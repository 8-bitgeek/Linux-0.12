#include "minicrt.h"

#define MAX_CMD_LEN 256
#define MAX_ARGC 8

void main() {
    char command[MAX_CMD_LEN];
    char * argv[MAX_ARGC];

    /* main loop */
    while (1) {
        printf("gshell> ");
        fgets(command, MAX_CMD_LEN, stdin);
        printf("%s", command);
    }
}