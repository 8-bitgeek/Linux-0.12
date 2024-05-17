#include <stdio.h>

#include "a.out.h"

int main(int argc, char * argv[]) {
    char * filename = argv[1];
    FILE * fp = fopen(filename, "r");
    struct exec exec_hdr;
    printf("sizeof struct exec: %ld\n", sizeof(struct exec));
    fread(&exec_hdr, sizeof(exec_hdr), 1, fp);
    printf("a_magic: 0%08o\n", exec_hdr.a_magic);
    printf("a_text: 0x%08x\n", exec_hdr.a_text);
    printf("a_data: 0x%08x\n", exec_hdr.a_data);
    printf("a_bss: 0x%08x\n", exec_hdr.a_bss);
    printf("a_syms: 0x%08x\n", exec_hdr.a_syms);
    printf("a_entry: 0x%08x\n", exec_hdr.a_entry);
    printf("a_trsize: 0x%08x\n", exec_hdr.a_trsize);
    printf("a_drsize: 0x%08x\n", exec_hdr.a_drsize);
    fclose(fp);

    return 0;
}
