/* The little C library the game needs when it is built without the MiNT libc (Makefile.ubuntu):
   memory and string primitives, and fopen/fread/fwrite/fclose on top of GEMDOS. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void *memcpy(void *dest, const void *src, size_t count) {
    unsigned char *d = (unsigned char *) dest;
    const unsigned char *s = (const unsigned char *) src;

    if ((((uintptr_t) d | (uintptr_t) s) & 1u) == 0) {
        uint32_t *dl = (uint32_t *) d;
        const uint32_t *sl = (const uint32_t *) s;

        for (; count >= 4; count -= 4) {
            *dl++ = *sl++;
        }
        d = (unsigned char *) dl;
        s = (const unsigned char *) sl;
    }
    while (count-- > 0) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    unsigned char *d = (unsigned char *) dest;
    const unsigned char *s = (const unsigned char *) src;

    if (d <= s || d >= s + count) {
        return memcpy(dest, src, count);
    }
    while (count-- > 0) {
        d[count] = s[count];
    }
    return dest;
}

void *memset(void *dest, int value, size_t count) {
    unsigned char *d = (unsigned char *) dest;
    const unsigned char byte = (unsigned char) value;

    if (((uintptr_t) d & 1u) == 0 && count >= 4) {
        uint32_t fill = byte;
        uint32_t *dl = (uint32_t *) d;

        fill |= fill << 8;
        fill |= fill << 16;
        for (; count >= 4; count -= 4) {
            *dl++ = fill;
        }
        d = (unsigned char *) dl;
    }
    while (count-- > 0) {
        *d++ = byte;
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t count) {
    const unsigned char *x = (const unsigned char *) a;
    const unsigned char *y = (const unsigned char *) b;

    for (; count > 0; --count, ++x, ++y) {
        if (*x != *y) {
            return (int) *x - (int) *y;
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    const char *end = text;

    while (*end != '\0') {
        ++end;
    }
    return (size_t) (end - text);
}

/* ---- files ---- */

long st_f_open(const char *name, int mode);
long st_f_create(const char *name);
long st_f_read(int handle, long len, void *buffer);
long st_f_write(int handle, long len, const void *buffer);
long st_f_close(int handle);

struct ST_FILE {
    int handle;
    int in_use;
};

static struct ST_FILE file_pool[2];

FILE *fopen(const char *name, const char *mode) {
    long handle;
    int slot;

    for (slot = 0; slot < 2 && file_pool[slot].in_use; ++slot) {
    }
    if (slot == 2) {
        return NULL;
    }
    handle = (mode[0] == 'w') ? st_f_create(name) : st_f_open(name, 0);
    if (handle < 0) {
        return NULL;
    }
    file_pool[slot].handle = (int) handle;
    file_pool[slot].in_use = 1;
    return &file_pool[slot];
}

size_t fread(void *buffer, size_t size, size_t count, FILE *file) {
    const long done = st_f_read(file->handle, (long) (size * count), buffer);

    return (done > 0 && size > 0) ? (size_t) done / size : 0;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *file) {
    const long done = st_f_write(file->handle, (long) (size * count), buffer);

    return (done > 0 && size > 0) ? (size_t) done / size : 0;
}

int fclose(FILE *file) {
    (void) st_f_close(file->handle);
    file->in_use = 0;
    return 0;
}
