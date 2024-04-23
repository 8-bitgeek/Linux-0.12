#include "minicrt.h"

#ifdef WIN32
#include <Windows.h>
#endif

extern int main(int argc, char * argv[]);
static void exit(int);

static void crt_fatal_error(const char * msg) {
    printf("fatal error: %s\n", msg);
    exit(1);
}

void mini_crt_entry(void) {
    int ret;

#ifdef WIN32
    int flag = 0;
#else
    int argc;
    char ** argv;
    char * ebp_reg = 0;
    // ebp_reg = %ebp
    asm("movl %%ebp, %0;" 
        : "=r" (ebp_reg));
    argc = *(int *)(ebp_reg + 4);
    argv = (char **)(ebp_reg + 8);
#endif

    if (!mini_crt_heap_init()) {
        crt_fatal_error("heap initialize failed!");
    }
    if (!mini_crt_io_init()) {
        crt_fatal_error("io initialize failed!");
    }
    ret = main(argc, argv);
    exit(ret);
}

static void exit(int exit_code) {
#ifdef WIN32
    ExitProcess();
#else
    // 调用 0x80 的 1 号系统调用.
    asm("movl %0, %%ebx;"
        "movl $1, %%eax;"
        "int $0x80;"
        "hlt;"
        : : "m"(exit_code));
#endif
}
