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
    unsigned a_magic;               /* Use macros N_MAGIC, etc for access */
    unsigned a_text;                /* length of text, in bytes */
    unsigned a_data;                /* length of data, in bytes */
    unsigned a_bss;                 /* length of uninitialized data area for file, in bytes */
    unsigned a_syms;                /* length of symbol table data in file, in bytes */
    unsigned a_entry;               /* start address */
    unsigned a_trsize;              /* length of relocation info for text, in bytes */
    unsigned a_drsize;              /* length of relocation info for data, in bytes */
};

int main() {
    printf("size of struct exec: %d\n", sizeof(struct exec));
    FILE * fd = fopen("./test", "r+");
    char * exec_ptr = malloc(sizeof(struct exec));
    int readed = fread(exec_ptr, sizeof(struct exec), 1, fd);
    printf("readed %d bytes.\n", readed);
    free(exec_ptr);
    return 0;
}