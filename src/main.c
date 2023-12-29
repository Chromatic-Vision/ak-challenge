#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#include "stb_image.h"

static uint8_t get_hue(uint8_t r, uint8_t g, uint8_t b) {
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

uint8_t* get_image(FILE* fp) {
	{
		uint8_t header_buffer[2];
		if (fread(header_buffer, 1, 2, fp) != 2) {
			fprintf(stderr, "error reading bitmap: incorrect header\n");
			return NULL;
		}
		if (header_buffer[0] != 'B' || header_buffer[1] != 'M') {
			fprintf(stderr, "error reading bitmap: unsupported type '%c%c'\n", header_buffer[0], header_buffer[1]);
			return NULL;
		}
	}
	uint32_t bitmap_size = 0;
	if (fread(&bitmap_size, 4, 1, fp) != 1) {
		fprintf(stderr, "error getting bitmap size\n");
		return NULL;
	}
	printf("bitmap file size: %u\n", bitmap_size);

	// fread(NULL, 2, 2, fp);
	fseek(fp, 4, SEEK_CUR);

	{
		uint32_t bm_offset = 0;
		if (fread(&bm_offset, 1, 4, fp) != 4) {
			fprintf(stderr, "error reading bitmap: incorrect offset for data\n");
			return NULL;
		}
	}

	return NULL;
}

int main(int argc, char** argv) {
	// ffmpeg -i test.mp4 -c:v bmp -f rawvideo -an -

	if (argc != 2) {
		fprintf(stderr, "USAGE: conv file\n");
		return 1;
	}

#define COMMAND_BUFFER_SIZE 1024
	char command_buffer[COMMAND_BUFFER_SIZE];

	snprintf(command_buffer, COMMAND_BUFFER_SIZE, "ffmpeg -i '%s' -c:v bmp -f rawvideo -an -", argv[1]);

	printf("%s\n", command_buffer);

	FILE* fp = popen(command_buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "failed to run ffmpeg\n");
		return 1;
	}

	unsigned int i = 0;
	while (/*!feof(fp)*/ i < 10) {
		// if (get_image(fp) == NULL) {
		//	fprintf(stderr, "error reading image\n");
		//	break;
		// }

		int width, height, components;
		unsigned char* data = stbi_load_from_file(fp, &width, &height, &components, 3);
		if (data == NULL) {
			fprintf(stderr, "corrupt image '%s'\n", stbi_failure_reason());
		}
		printf("stbi image addres: %lx\n", (size_t)data);
		stbi_image_free(data);

		printf("loaded image %u, %dx%d\n", i, width, height);
		i++;
	}

	int code = pclose(fp);
	if (code < 0) {
		fprintf(stderr, "error waiting on ffmpeg\n");
		return 1;
	} else {
		printf("ffmpeg returned %d\n", code / 0xFF);

		if (code != 0) {
			return 1;
		}
	}

	return 0;
}
