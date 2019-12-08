#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#include "user/limits.h"

#define DEBUG 0

//Please make sure:
//  size of path is PATH_MAX
void find_sub(char* path, const char* name) {
    struct dirent de;   //dir entry
    struct stat st;
    int fd;
    char* base;

    int len_path;

    if ((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch (st.type) {
    case T_FILE:
        base = basename(path);
        if (strcmp(base, name) == 0) {
            printf("%s\t%d %d %l\n", basename(path), st.type, st.ino, st.size);
        }
        break;

    case T_DIR:
        len_path = strlen(path);
        if (len_path + 1 + strlen(de.name) + 1 > PATH_MAX) {
            printf("find: path too long\n");
            break;
        }
        base = path + len_path;
        *(base++) = '/';
        read(fd, &de, sizeof(de));    //.
        read(fd, &de, sizeof(de));    //..
        //read(fd, &de, sizeof(de)) == sizeof(de);
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            memmove(base, de.name, DIRSIZ);
            base[DIRSIZ] = 0;
            find_sub(path, name);
        }
        path[len_path] = 0;
        break;
    
    case T_DEVICE:
        #if DEBUG
        fprintf(2, "a device\n");
        #endif
        break;
    }
    close(fd);
}

//default: -iname
void find(const char* path, const char* name) {
    char buf[PATH_MAX];
    strcpy(buf, path);
    find_sub(buf, name);
}

int main(int argc, char** argv) {
    if (argc == 3) {
        find(argv[1], argv[2]);
    } else {
        fprintf(2, "Usage: find <dir> <name>\n");
        exit(1);
    }
    exit(0);
}
