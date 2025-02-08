#include "minicrt.h"
#include "command.h"

#define MAX_CMD_LEN 256
#define MAX_ARGC 8


void main() {
    char command[MAX_CMD_LEN];
    char * argv[MAX_ARGC];
    struct stat * st;
    int * fd;
    uint argc;

    /* main loop */
    while (1) {
        printf("gshell> ");
        fgets(command, MAX_CMD_LEN, stdin);

        argc = parse_command(command, argv);

        printf("%d args\n", argc);

        if (argc == 0) {
            continue;
        }

        execute_command(argv);
    }
}


uint parse_command(char * command, char * argv[]) {
    int argc = 0;

    while (*command != '\0') {
        while (*command == ' ') {                       /* skip blank chars */
            command++;
        }

        if (*command == '\0') {                         /* after skip all blank chars, just remain a '\0' */
            break;                                      /* means there's no comamnd at all */
        }

        argv[argc++] = command;

        while (*command != ' ' && *command != '\0') {
            command++;
        }

        if (*command != '\0') {                         /* if any other blank chars, replace it with '\0' */
            *command++ = '\0';
        }
    }

    argv[argc] = NULL;

    return argc;
}

uint execute_command(char * argv[]) {
    DIR * dir;
    struct dirent * dir_entry;
    int i = 0;
    if (strcmp(argv[0], "exit") == 0) {
        printf("GoodBye!\n");
        exit(0);
    } else if (strcmp(argv[0], "cd") == 0) {
        if (argv[1] == NULL) {
            printf("cd: missing argument\n");
        } else {
            if (chdir(argv[1]) != 0) {
                printf("cd: no such directory: %s\n", argv[1]);
            }
        }
    } else if (strcmp(argv[0], "ls")) {
        dir = opendir(".");
        while (i++ <= 5) {
            dir_entry = readdir(dir);
            printf("%s\t", dir_entry->name);
        }
    } else {
        printf("Command not found: %s\n", argv[0]);
    }
}