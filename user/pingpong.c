#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char** argv) {
    int parent_fd[2], child_fd[2];
    char buf[111];

    if (argc == 1) {
        pipe(parent_fd);
        pipe(child_fd);
        if (fork() == 0) {
            //child
            close(parent_fd[1]);
            close(child_fd[0]);

            read(parent_fd[0], buf, 5);
            printf("%s", buf);
            close(parent_fd[0]);

            write(child_fd[1], "pong\n", 5);
            close(child_fd[1]);
        } else {
            //parent
            close(parent_fd[0]);
            close(child_fd[1]);

            write(parent_fd[1], "ping\n", 5);
            close(parent_fd[1]);

            read(child_fd[0], buf, 5);
            printf("%s", buf);
            close(child_fd[0]);
        }
    } else {
        fprintf(2, "Usage: %s\n", argv[0]);
        exit(1);
    }
    exit(0);
}
