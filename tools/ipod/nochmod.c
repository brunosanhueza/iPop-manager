#define _GNU_SOURCE
#include <sys/stat.h>
int chmod(const char *p, mode_t m) { return 0; }
int fchmod(int fd, mode_t m) { return 0; }
int fchmodat(int d, const char *p, mode_t m, int f) { return 0; }
