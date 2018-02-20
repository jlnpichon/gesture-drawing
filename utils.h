#ifndef UTILS_H
#define UTILS_H

#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))

int is_image(const char *filename);

void seconds_to_time(char *str, size_t size, int seconds);

#endif /* UTILS_H */
