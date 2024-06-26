// sizeof(struct exec) == 32bytes.
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

#define ZMAGIC 0413
#define CODE_LIMIT 0x3000000
#define TEXT_OFFSET 0x400
