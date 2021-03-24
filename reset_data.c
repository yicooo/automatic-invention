#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "scull_ioctl.h"

int main()
{

	int fd = open("/dev/scull", O_RDWR);
	ioctl(fd, SCULL_DATA_RESET);
	close(fd);

	return 0;
}
