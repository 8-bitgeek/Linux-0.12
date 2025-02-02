#include "minicrt.h"


int mini_crt_io_init() {
    return 1;
}

FILE * fopen(const char * filename, const char * mode) {
    int fd = -1;
    int flags = 0;
    int access = 00700;                         /* permission flag to create file */

    if (strcmp(mode, "w") == 0) {
        flags |= O_WRONLY | O_CREAT | O_TRUNC;
    }
    if (strcmp(mode, "w+") == 0) {
        flags |= O_RDWR | O_CREAT | O_TRUNC;
    }
    if (strcmp(mode, "r") == 0) {
        flags |= O_RDONLY;
    }
    if (strcmp(mode, "r+") == 0) {
        flags |= O_RDWR | O_CREAT;
    }

    fd = open(filename, flags, access);

    return (FILE *)fd;
}

int fread(void * buffer, int size, int count, FILE * stream) {
    return read((int)stream, buffer, size * count);
}

int fwrite(const void * buffer, int size, int count, FILE * stream) {
    return write((int)stream, buffer, size * count);
}

int fclose(FILE * fp) {
    return close((int)fp);
}

int fseek(FILE * fp, int offset, int set) {
    return seek((int)fp, offset, set);
}

int fputc(int c, FILE * stream) {
    if (fwrite(&c, 1, 1, stream) != 1) {
        return EOF;
    } else {
        return c;
    }
}

int fputs(const char * str, FILE * stream) {
    int len = strlen(str);
    if (fwrite(str, 1, len, stream) != len) {
        return EOF;
    } else {
        return len;
    }
}

int fgetc(FILE * stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) != 1) {
        return EOF;
    }
    return (int) c;
}

char * fgets(char * str, int size, FILE * stream) {
    int i = 0;
    int c;

    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) {
                return NULL;
            }
            break;
        }
        str[i++] = (char) c;

        if (c == '\n') {            // if get return, finish read too.
            break;
        }
    }
    str[i] = '\0';
    return str;
}