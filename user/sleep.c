#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char** argv) {
    if (argc == 2) {
        sleep(atoi(argv[1]));
    } else {
        fprintf(2, "Usage: %s <time>\n", argv[0]);
        exit(1);
    }
    exit(0);
}
