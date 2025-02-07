#include "../minicrt/include/minicrt.h"

/*
 * readelf: 
 *      -h: read elf file header
 *      -l: read program headers
 *      -S: section header
 *      -s: symbol table
 *      -r: relocation table
 *      -d: dynamic section
 *      -s <section>: read specific section hex info
 */

struct exec {
    unsigned long a_magic;          /* Use macros N_MAGIC, etc for access */
    unsigned a_text;                /* length of text, in bytes */
    unsigned a_data;                /* length of data, in bytes */
    unsigned a_bss;                 /* length of uninitialized data area for file, in bytes */
    unsigned a_syms;                /* length of symbol table data in file, in bytes */
    unsigned a_entry;               /* start address */
    unsigned a_trsize;              /* length of relocation info for text, in bytes */
    unsigned a_drsize;              /* length of relocation info for data, in bytes */
};

int main() {
    /* C89 standard: All variable declarations must appear at the beginning of a block (a function or a code block). */
    struct exec * exec_ptr;
    FILE * fd;
    void * ptr;
    int readed;
    struct dirent * dir_entry;
    DIR * dir = opendir("/home/gldwolf");
    while (1) {
        dir_entry = readdir(dir);
        printf("dirent->name: %s\n", dir_entry->name);
    }
    fd = fopen("./test", "r+");
    ptr = malloc(sizeof(struct exec));
    readed = fread(ptr, sizeof(struct exec), 1, fd);
    printf("readed %d bytes.\n", readed);
    exec_ptr = (struct exec *) ptr;
    printf("a_magic = 0x%x, a_text = 0x%x, a_data = 0x%x, a_bss = 0x%x, a_syms = 0x%x, a_entry = 0x%x, a_trsize = 0x%x, a_drsize = 0x%x\n", 
            exec_ptr->a_magic, exec_ptr->a_text, exec_ptr->a_data, exec_ptr->a_bss, exec_ptr->a_syms, exec_ptr->a_entry, exec_ptr->a_trsize, exec_ptr->a_drsize);
    free(ptr);
    while (1) {}
    return 0;
}