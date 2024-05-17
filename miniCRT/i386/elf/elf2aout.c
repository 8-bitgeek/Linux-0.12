#include <stdio.h>
#include <elf.h>
#include <stdlib.h>
#include <string.h>

#include "a.out.h"

int main() {
    const char * filename = "test";
    FILE * fp = fopen(filename, "r");
    Elf32_Ehdr ehdr;
    if (fp != NULL) {
        fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp);
    }
    printf("Number of Section headers: %d\n", ehdr.e_shnum);
    printf("Section header table offset: %d\n", ehdr.e_shoff);
    Elf32_Shdr shdr[ehdr.e_shnum];
    fseek(fp, ehdr.e_shoff, 0);
    fread(shdr, sizeof(Elf32_Shdr), ehdr.e_shnum, fp);


    int sh_st_offset = shdr[ehdr.e_shstrndx].sh_offset;
    char sh_st_data[shdr[ehdr.e_shstrndx].sh_size];
    fseek(fp, sh_st_offset, 0);
    fread(sh_st_data, shdr[ehdr.e_shstrndx].sh_size, 1, fp);

    int a_text, a_data, a_bss = 0;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        printf("section %s offset: 0x%08x.\n", &sh_st_data[shdr[i].sh_name], shdr[i].sh_offset);
        // code section
        if (!strcmp(&sh_st_data[shdr[i].sh_name], ".text")) {
            a_text = shdr[i].sh_size;
        } else if (!strcmp(&sh_st_data[shdr[i].sh_name], ".data")) {
            a_data = shdr[i].sh_size;
        } else if (!strcmp(&sh_st_data[shdr[i].sh_name], ".bss")) {
            a_bss = shdr[i].sh_size;
        }
    }

    struct exec exec_hdr;
    exec_hdr.a_magic = ZMAGIC;
    exec_hdr.a_text = a_text;
    exec_hdr.a_data = a_data;
    exec_hdr.a_bss = a_bss;
    printf("Size of .text: 0x%08x .data: 0x%08x .bss: 0x%08x\n", a_text, a_data, a_bss);

    fclose(fp);
    return 0;
}
