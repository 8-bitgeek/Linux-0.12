#include "tinylib.h"

char * str = "Hello world!\n";

void print(int cnt) {
    asm(
        "movl %0, %%edx;"
        "movl %1, %%ecx;"
        "movl $0, %%ebx;"
        "movl $4, %%eax;"
        "int $0x80;"
        : : "r"(cnt), "r"(str) : "edx", "ecx", "ebx"
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
    int cnt = sum(10, 3);
    print(cnt);
    exit();
}
