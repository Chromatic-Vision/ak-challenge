#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

// TODO: use jpeg to compress chunks
// TODO: maybe use jpeg to compress 1-bit?

static uint8_t rgb2h(uint8_t r, uint8_t g, uint8_t b) {
	uint8_t max = r > g ? r : g;
	max = b > max ? b : max;

	uint8_t min = r < b ? r : g;
	min = b < min ? b : min;

	uint8_t delta = max - min;
	if (delta == 0.0) {
		return 0;
	}

	float hue = 0;
	if (r == max) {
		hue = (float)(g - b) / (float)delta;
	} else if (g == max) {
		hue = 2.0 + (float)(b - r) / (float)delta;
	} else {
		hue = 4.0 + (float)(r - g) / (float)delta;
	}

	hue *= 42.5;
	if (hue < 0) {
		hue += 255.0;
	}

	return (uint8_t)hue;
}
static uint8_t rgb2v(uint8_t r, uint8_t g, uint8_t b) {
	return (uint8_t)(((uint16_t)r + (uint16_t)g + (uint16_t)b) / 3);
}
static uint8_t rgb2s(uint8_t r, uint8_t g, uint8_t b) {
	uint8_t max = r > g ? r : g;
	max = b > max ? b : max;

	uint8_t min = r < b ? r : g;
	min = b < min ? b : min;

	uint8_t delta = max - min;
	if (delta == 0.0) {
		return 0;
	}

	return (uint8_t)(((float)delta / 255.0) / ((float)rgb2v(r, g, b) / 255.0) * 255);
}

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

unsigned int write_size = 0;
void jpeg_write_callback(void* context, void* data, int size) {
	if (fwrite(data, 1, size, context) != size) {
		fprintf(stderr, "error writing jpeg to file\n");
	}
	write_size += size;
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

	uint8_t* image = malloc(out_width*out_height*3);
	uint8_t* chunks = malloc((out_width / CHUNK_SIZE)*(out_height / CHUNK_SIZE) * 3);
	if (image == NULL || chunks == NULL) {
		fprintf(stderr, "allocating image failed\n");

		if (image != NULL) {
			free(image);
		}
		if (chunks != NULL) {
			free(chunks);
		}

		pclose(fp);
		fclose(outfp);
		return 1;
	}

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
	while (/*!feof(fp)*/ i < 10) {
		if (fread(image, 3, out_width * out_height, fp) != out_width * out_height) {
			fprintf(stderr, "image too small\n");
			goto fail;
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


				uint8_t h = rgb2h(r, g, b);
				chunks[x + y * (out_width / CHUNK_SIZE)] = h;
				uint8_t s = rgb2s(r, g, b);
				chunks[x + y * (out_width / CHUNK_SIZE) + 1] = s;
				uint8_t v = (uint8_t)(((uint16_t)r + (uint16_t)g + (uint16_t)b) / 3);
				chunks[x + y * (out_width / CHUNK_SIZE) + 2] = v;
			}
		}

		// TODO: write chunk size and data

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

		// {
		// 	unsigned int s = (out_width / CHUNK_SIZE)*(out_height / CHUNK_SIZE) * 3;
		// 	if (fwrite(chunks, 1, s, outfp) != s) {
		// 		fprintf(stderr, "error writing chunks\n");
		// 		goto fail;
		// 	}
		// }

		printf("loaded image %u\n", i);
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
