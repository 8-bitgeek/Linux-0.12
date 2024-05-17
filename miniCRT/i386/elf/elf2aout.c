#include <stdio.h>
#include <elf.h>
#include <stdlib.h>
#include <string.h>

#include "a.out.h"

int write_aout(struct exec * fileheader, void * textbuf, unsigned text_size);
void * read_text(FILE * fp, unsigned offset, unsigned a_text_size);

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

    int a_text_size, a_data_size, a_bss_size, a_text_off, a_data_off, a_bss_off = 0;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        printf("section %s offset: 0x%08x.\n", &sh_st_data[shdr[i].sh_name], shdr[i].sh_offset);
        // code section
        if (!strcmp(&sh_st_data[shdr[i].sh_name], ".text")) {
            a_text_size = shdr[i].sh_size;
            a_text_off = shdr[i].sh_offset;
        } else if (!strcmp(&sh_st_data[shdr[i].sh_name], ".data")) {
            a_data_size = shdr[i].sh_size;
            a_data_off = shdr[i].sh_offset;
        } else if (!strcmp(&sh_st_data[shdr[i].sh_name], ".bss")) {
            a_bss_size = shdr[i].sh_size;
            a_bss_off = shdr[i].sh_offset;
        }
    }

    struct exec exec_hdr = {ZMAGIC, a_text_size, a_data_size, a_bss_size, 0, .a_entry = 0, 0, 0};
    int s = 0;
    if ((s = (a_text_size + a_data_size + a_bss_size)) < CODELIMIT) {
        printf("[WARN] code size [0x%x] bigger than Linux-0.12 demand[0x%x]!\n", s, CODELIMIT);
    }
    printf("Size of .text: 0x%08x, off: 0x%08x;\n\
           .data: 0x%08x, off: 0x%08x;\n\
           .bss: 0x%08x, off: 0x%08x;\n", 
           a_text_size, a_text_off, 
           a_data_size, a_data_off, 
           a_bss_size, a_bss_off);

    void * text = read_text(fp, a_text_off, a_text_size);

    int writed = write_aout(&exec_hdr, text, a_text_size);
    if (writed) {
        printf("Write a.out: %s!\n", writed ? "Succeed" : "Failed");
    }

    fclose(fp);
    return 0;
}

int write_aout(struct exec * fileheader, void * textbuf, unsigned text_size) {
    FILE * aout = fopen("a.out", "w");
    int writed = fwrite(fileheader, sizeof(struct exec), 1, aout);
    fseek(aout, 0x1000, 0);
    writed = fwrite(textbuf, text_size, 1, aout);
    fclose(aout);
    return writed;
}

void * read_text(FILE * fp, unsigned offset, unsigned a_text_size) {
    void * text = malloc(a_text_size);
    fseek(fp, offset, 0);
    fread(text, a_text_size, 1, fp);
    return text;
}
