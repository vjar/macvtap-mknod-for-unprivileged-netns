#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdio.h>

int32_t main(int32_t, char *[]) {
	int32_t rv = mknod("testnod", S_IFCHR, makedev(237, 2));
	if (rv) {
		perror("mknod");
		return 1;
	}
	return 0;
}
