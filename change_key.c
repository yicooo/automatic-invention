#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "scull_ioctl.h"

int main(int argc, char *argv[])
{

	custom_key test;
	test.size = strlen(argv[1]);
	memcpy(test.key,argv[1],strlen(argv[1]));
	int fd = open("/dev/scull", O_RDWR);
	ioctl(fd, SCULL_CHANGE_KEY, &test);
	close(fd);

	return 0;
}
