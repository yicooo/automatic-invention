#ifndef __SCULL_H
#define __SCULL_H

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

typedef struct
{
    int size;
    char key[30];
} custom_key;

#define SCULL_IOC_MAGIC 'k'
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOC_MAXNR 12
#define SCULL_CHANGE_KEY _IOR(SCULL_IOC_MAGIC, 1, custom_key)
#define SCULL_DATA_RESET _IO(SCULL_IOC_MAGIC, 2)

#endif
