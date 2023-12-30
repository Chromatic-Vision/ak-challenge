#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"

#define JPEG_QUALITY 5  // 1-100, 1 is lowest quality

#define CHUNK_TYPE_VIDEO 0

// header:
//   magic: "ak-c"
//   width u32
//   height u32
//   fps u16
// chunk:
//   end: 0 u8
//   type u8
//   size u32
//   data (TODO)
// end:
//   end: 1 u8
//
// video chunk:
//   jpeg size u32
//   chunk jpeg
//   p_size u32
//   p: (multiple times)
//     data u1
//     amount u7
//     (rle)

// store 24-bit hsv of each chunk  (value as in hsv)
// 1-bit value of each pixel
//   each pixel has a 4-bit value
//   each frame if the 1-bit value is 1 add 1 to the 4-bit value, else subtract 1 from the 4-bit value
//   if value is b1111, add b11110000 to the chunk value
//   RLE it

// TODO: maybe use jpeg to compress 1-bit?

#define CHUNK_SIZE 8
static uint8_t average(uint8_t* data, unsigned int skip, unsigned int stride) {
#if CHUNK_SIZE*CHUNK_SIZE >= 2<<16
 #warning "chunk size could cause integer overflow"
#endif
#if CHUNK_SIZE >= 1<<8
 #error "chunk size is too big"
#endif

	uint16_t total = 0;

	for (unsigned int y = 0; y < CHUNK_SIZE; y++) {
		for (unsigned x = 0; x < CHUNK_SIZE; x++) {
			total += data[x*skip];
		}
		data = (uint8_t*)((size_t)data + (size_t)stride);
	}

	return total / (CHUNK_SIZE * CHUNK_SIZE);
}

struct {
	unsigned char* buffer;
	size_t size;
	size_t cap;
} buffer;

unsigned int write_size = 0;
void jpeg_write_callback(void* context, void* data, int size) { // TODO: also write to a buffer
	if (fwrite(data, 1, size, context) != size) {
		fprintf(stderr, "error writing jpeg to file\n");
	}
	write_size += size;

	if (buffer.buffer == NULL) {
		buffer.cap = 128;
		buffer.buffer = malloc(buffer.cap);
		if (buffer.buffer == NULL) {
			fprintf(stderr, "malloc failed\n");
			exit(1);
		}
	}
	if (buffer.size + size >= buffer.cap) {
		buffer.cap = buffer.size + size;
		buffer.buffer = realloc(buffer.buffer, buffer.cap);
		if (buffer.buffer == NULL) {
			fprintf(stderr, "realloc failed\n");
			exit(1);
		}
	}

	memcpy(buffer.buffer + buffer.size, data, size);
	buffer.size += size;
}

// 1 = succes, 0 = failure
int write_rle(FILE* fp, int value, int amount) {
	if (value < 0 || value > 1) {
		fprintf(stderr, "incorrect rle value %d\n", value);
		return 0;
	}
	if (amount < 0 || amount > 0xF) {
		if (amount < 0) {
			fprintf(stderr, "rle negative (%d)\n", amount);
		} else {
			fprintf(stderr, "rle too long (%d)\n", amount);
		}
		return 0;
	}

	uint8_t out = (value << 7) | amount;
	if (fwrite(&out, 1, 1, fp) != 1) {
		fprintf(stderr, "writing rle output failed\n");
		return 0;
	}
	return 1;
}

int main(int argc, char** argv) {
	// ffmpeg -i test.mp4 -c:v bmp -f rawvideo -an -

	if (sizeof(unsigned int) != 4) {
		fprintf(stderr, "AA\n");
		return 1;
	}

	if (argc != 3) {
		fprintf(stderr, "USAGE: conv infile outfile\n");
		return 1;
	}

	unsigned int out_width = 1920, out_height = 1080;
	fprintf(stderr, "no output resolution specified, assuming 1920x1080\n");

#define COMMAND_BUFFER_SIZE 1024
	char command_buffer[COMMAND_BUFFER_SIZE];

	// -an skips audio
	snprintf(command_buffer, COMMAND_BUFFER_SIZE, "ffmpeg -i '%s' -f rawvideo -pix_fmt rgb24 -s 1920x1080 -an -", argv[1]);

	printf("%s\n", command_buffer);

	FILE* fp = popen(command_buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "failed to start ffmpeg\n");
		return 1;
	}

	FILE* outfp = fopen(argv[2], "w");
	if (outfp == NULL) {
		fprintf(stderr, "error opening file '%s' for writing\n", argv[2]);

		pclose(fp);
		return 1;
	}

	buffer.buffer = NULL;
	buffer.cap = 0;
	buffer.size = 0;

	uint8_t* image = malloc(out_width*out_height*3);
	uint8_t* offset = malloc(out_width*out_height);
	uint8_t* chunks = malloc((out_width / CHUNK_SIZE)*(out_height / CHUNK_SIZE) * 3);
	if (image == NULL || chunks == NULL || offset == NULL) {
		fprintf(stderr, "allocating image failed\n");

		if (image != NULL) {
			free(image);
		}
		if (offset != NULL) {
			free(offset);
		}
		if (chunks != NULL) {
			free(chunks);
		}

		pclose(fp);
		fclose(outfp);
		return 1;
	}
	memset(offset, 0, out_width*out_height);

	{
		uint8_t header[] = {'a', 'k', '-', 'c'};
		if (fwrite(header, 1, 4, outfp) != 4) {
			fprintf(stderr, "error writing magic\n");
			goto fail;
		}
	}
	if (fwrite(&out_width, 4, 1, outfp) != 1) {
		fprintf(stderr, "error writing width\n");
		goto fail;
	}
	if (fwrite(&out_height, 4, 1, outfp) != 1) {
		fprintf(stderr, "error writing height\n");
		goto fail;
	}
	{
		uint16_t fps = 0;
		if (fwrite(&fps, 2, 1, outfp) != 1) {
			fprintf(stderr, "error writing fps\n");
			goto fail;
		}
	}

	unsigned int i = 0;
	while (/*!feof(fp)*/ i < 10 /*1*/) {
		{
			size_t s = fread(image, 3, out_width * out_height, fp);
			if (s != out_width * out_height) {
				if (s == 0) {
					fprintf(stderr, "ZEROOOO!!! (assuming this means end of input file)\n");
					break;
				}

				fprintf(stderr, "image too small\n");
				goto fail;
			}
		}

		{ // write that this chunk exists
			uint8_t end = 0;
			if (fwrite(&end, 1, 1, outfp) != 1) {
				fprintf(stderr, "error writing end of file\n");
				goto fail;
			}
		}
		{ // write chunk type
			uint8_t type = CHUNK_TYPE_VIDEO;
			if (fwrite(&type, 1, 1, outfp) != 1) {
				fprintf(stderr, "error chunk type\n");
				goto fail;
			}
		}

		for (unsigned int y = 0; y < out_height / CHUNK_SIZE; y++) {
			for (unsigned int x = 0; x < out_width / CHUNK_SIZE; x++) {
				uint8_t r = average(&image[x * CHUNK_SIZE + y * CHUNK_SIZE * out_width], 3, out_width);
				uint8_t g = average(&image[x * CHUNK_SIZE + y * CHUNK_SIZE * out_width + 1], 3, out_width);
				uint8_t b = average(&image[x * CHUNK_SIZE + y * CHUNK_SIZE * out_width + 2], 3, out_width);


				chunks[x + y * (out_width / CHUNK_SIZE)] = r;
				chunks[x + y * (out_width / CHUNK_SIZE) + 1] = g;
				chunks[x + y * (out_width / CHUNK_SIZE) + 2] = b;
			}
		}

		// TODO: write chunk size

		{
			write_size = 0;

			long pos = ftell(outfp);

			if (fwrite(&write_size, 4, 1, outfp) != 1) {
				fprintf(stderr, "error writing placeholder jpeg size\n");
				goto fail;
			}

			if (stbi_write_jpg_to_func((stbi_write_func*)jpeg_write_callback, outfp, out_width / CHUNK_SIZE, out_height / CHUNK_SIZE, 3, chunks, JPEG_QUALITY) == 0) {
				fprintf(stderr, "writing image failed\n");
				goto fail;
			}

			if (fseek(outfp, pos, SEEK_SET) < 0) {
				fprintf(stderr, "could not seek\n");
				goto fail;
			}
			if (fwrite(&write_size, 4, 1, outfp) != 1) {
				fprintf(stderr, "error writing jpeg size\n");
				goto fail;
			}
			if (fseek(outfp, 0, SEEK_END) < 0) {
				fprintf(stderr, "could not seek\n");
				goto fail;
			}
		}

		{
			int width, height, channels;

			uint8_t* d_image = stbi_load_from_memory(buffer.buffer, buffer.size, &width, &height, &channels, 3);
			if (d_image == NULL) {
				fprintf(stderr, "loading image failed '%s'\n", stbi_failure_reason());
				goto fail;
			}
			if (channels != 3) {
				fprintf(stderr, "file has incorrect amount of channels %d\n", channels);
				goto fail;
			}

			int last = -1;
			int amount = 0;
			for (unsigned int y = 0; y < out_height; y++) {
				for (unsigned int x = 0; x < out_width; x++) {
					uint8_t* d_rgb = &d_image[x / CHUNK_SIZE + y / CHUNK_SIZE * width];
					uint8_t d_val = (uint8_t)(((uint16_t)d_rgb[0] + (uint16_t)d_rgb[1] + (uint16_t)d_rgb[2]) / 3);

					uint8_t* a_rgb = &image[x + y * out_width];
					uint8_t a_val = (uint8_t)(((uint16_t)a_rgb[0] + (uint16_t)a_rgb[1] + (uint16_t)a_rgb[2]) / 3);

					int value = 0;
					if (d_val + (offset[x + y * out_width] << 4) < a_val) {
						// TODO: write to file
						if (x < 100) {
							printf("1");
						}

						if (offset[x + y * out_width] < 0xF) {
							offset[x + y * out_width]++;
						}
						value = 1;
					} else {
						if (x < 100) {
							printf("0");
						}

						if (offset[x + y * out_width] > 0) {
							offset[x + y * out_width]--;
						}
						value = 0;
					}

					if (value != last) {
						if (amount > 0) {
							if (write_rle(outfp, value, amount) == 0) {
								goto fail; // it has already printed an error message
							}
						}
						last = value;
						amount = 0;
					}
					if (amount == 127) {
						if (write_rle(outfp, value, amount) == 0) {
							goto fail; // it has already printed an error message
						}
						amount = 0;
					}
				}
				printf("\n");
			}

			stbi_image_free(d_image);
		}

		// {
		// 	unsigned int s = (out_width / CHUNK_SIZE)*(out_height / CHUNK_SIZE) * 3;
		// 	if (fwrite(chunks, 1, s, outfp) != s) {
		// 		fprintf(stderr, "error writing chunks\n");
		// 		goto fail;
		// 	}
		// }

		printf("loaded image %u\n", i);
		// {
		// 	void* temp = last_image;
		// 	last_image = image;
		// 	image = temp;
		// }
		i++;
	}

	{
		uint8_t end = 1;
		if (fwrite(&end, 1, 1, outfp) != 1) {
			fprintf(stderr, "error writing end of file\n");
			goto fail;
		}
	}

	int failed;

	goto not_fail;
    fail:
	failed = 1;
	goto defer;
    not_fail:
	failed = 0;

    defer:
	free(image);
	free(offset);
	free(chunks);
	fclose(outfp);

	int code = pclose(fp);
	if (code < 0) {
		fprintf(stderr, "error waiting on ffmpeg\n");
		return 1;
	} else {
		fprintf(stderr, "ffmpeg returned %d\n", code / 0xFF);

		if (code != 0) {
			return 1;
		}
	}

	return failed;
}
