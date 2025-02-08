#include "minicrt.h"
#include "dirent.h"

DIR * opendir(const char * name) {
    DIR * dir;
    int fd = open(name, 0, 0);

    if (fd < 0) {
        return NULL;
    }

    dir = (DIR *) malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_size = 0;
    return dir;
}

struct dirent * readdir(DIR * dir) {
    int ret;
    struct dirent * entry;

    if (!dir || dir->fd < 0) {
        return NULL;
    }

    if (dir->buf_pos >= dir->buf_size) {
        dir->buf_size = read(dir->fd, dir->buf, DIR_BUF_SIZE);
        if (dir->buf_size <= 0) {
            return NULL;
        }
        dir->buf_pos = 0;
    }
    entry = (struct dirent *) (dir->buf + dir->buf_pos);
    dir->buf_pos += sizeof(struct dirent);

    return entry;
}

int closedir() {

}