#include "tinylib.h"

void print(char * str, int cnt) {
    asm(
        "movl %0, %%edx;"
        "movl %1, %%ecx;"
        "movl $0, %%ebx;"
        "movl $4, %%eax;"
        "int $0x80;"
        : : "r"(cnt), "r"(str)
    );
}

void exit() {
    asm (
        "movl $42, %ebx;"
        "movl $1, %eax;"
        "int $0x80;"
    );
}

void nomain() {
    char * str = "Hello world!\n";
    int cnt = sum(10, 3);
    print(str, cnt);
    exit();
}
