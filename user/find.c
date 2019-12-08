#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#include "user/basename.h"
#include "user/limits.h"
#include "user/lib/fmtname.h"

int kmp(const char s[], int m, const char p[], int n, const int fail[]) {
	int j = 0, i;
	for(i = 0; i < m; i++) {
		while(j != -1 && s[i] != p[j])
			j = fail[j];
		if(++j == n) {
			return 1;
			j = fail[j];
		}
	}
	return 0;
}

//The length of fail will be (n+1)
void GetFail(int fail[], int n, const char p[]) {
	int i = 0, k = -1;
	fail[0] = -1;
	while(i < n)
		if(k == -1 || p[i] == p[k])
			fail[++i] = ++k;
		else
			k = fail[k];
}

//Please make sure:
//  size of path is PATH_MAX
void find_sub(char* path, char* name, int len_name, int fail[]) {
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
        if (kmp(base, strlen(base), name, len_name, fail)) {
            printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);
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
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            memmove(base, de.name, DIRSIZ);
            base[DIRSIZ] = 0;
            find_sub(path, name, len_name, fail);
        }
        path[len_path] = 0;
        break;
    }
}

//default: -iname
void find(char* path, char* name) {
    int fail[PATH_MAX];
    int len_name = strlen(name);
    GetFail(fail, len_name, name);
    find_sub(path, name, len_name, fail);
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
