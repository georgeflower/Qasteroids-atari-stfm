/* Stand-in for <stdio.h>: just the whole-file reads and writes the game does (implemented in rt/libc.c on GEMDOS). */
#ifndef ST_STDIO_H
#define ST_STDIO_H

#include <stddef.h>

typedef struct ST_FILE FILE;

FILE *fopen(const char *name, const char *mode);
size_t fread(void *buffer, size_t size, size_t count, FILE *file);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *file);
int fclose(FILE *file);

#endif
