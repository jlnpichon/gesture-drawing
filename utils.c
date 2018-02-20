#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

static const char *image_extensions[] = {
	"bmp",
	"exif",
	"gif",
	"jfif"
	"jpeg",
	"jpg",
	"png",
	"pbm",
	"pgm",
	"ppm",
	"tiff",
	"webp",
};

int is_image(const char *filename)
{
	int i;
	size_t len = strlen(filename);
	const char *ext = filename + len;

	while (ext >= filename && *ext != '.')
		ext--;

	if (ext == filename + len ||
			ext < filename ||
			*ext != '.')
		return 0;

	ext++;
	if (ext - filename > len)
		return 0;

	for (i = 0; i < ARRAY_SIZE(image_extensions); i++) {
		if (strlen(ext) == strlen(image_extensions[i]) &&
				!strcasecmp(image_extensions[i], ext))
			return 1;
	}

	return 0;
}

void seconds_to_time(char *str, size_t size, int seconds)
{
	int h;
	int m;

	h = seconds / 3600;
	seconds -= h * 3600;
	
	m = seconds / 60;
	seconds -= m * 60;

	snprintf(str, size, "%02d:%02d:%02d", h, m, seconds);
}
