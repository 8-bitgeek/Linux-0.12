#include "minicrt.h"

extern int main(int argc, char * argv[]);

void exit(int exit_code);

static void crt_fatal_error(const char * msg) {
    printf("fatal error: %s\n", msg);
    exit(1);
}

void mini_crt_entry(void) {
    /* push %ebp */
    /* mov %esp, %ebp */
    /* The C compiler will inserts stack frame management code 
       before user code if without -fomit-frame-pointer option, 
       like `push %ebp; movl %esp, %ebp;`, so now ebp = esp + 4.
       About stack frame see: CLK p57 */
    int ret;
    int argc;
    char ** argv;
    char * ebp_reg = 0;
    asm("movl %%ebp, %0;" 
        : "=r" (ebp_reg));      

    /* since do_execve() does not use a call instruction, 
       the return address is not pushed onto the stack, 
       so (ebp + 4) directly points to argc. */
    argc = *(int *)(ebp_reg + 4);
    argv = (char **)(ebp_reg + 8);

    if (!mini_crt_heap_init()) {
        crt_fatal_error("heap initialize failed!");
    }

    if (!mini_crt_io_init()) {
        crt_fatal_error("IO initialize failed!");
    }
    ret = main(argc, argv);
    exit(ret);
    /* pop %ebp */
    /* ret */
}

void exit(int exit_code) {
    /* syscall: sys_exit(), eax = 1 */
    asm("movl %0, %%ebx \n\t"
        "movl $1, %%eax \n\t"
        "int $0x80      \n\t"
        "hlt            \n\t"
        :: "m" (exit_code));
}
