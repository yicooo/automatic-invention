#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

#include <asm/switch_to.h> /* cli(), *_flags */
#include <linux/uaccess.h> /* copy_*_user */

#include "scull_ioctl.h"

#define SCULL_MAJOR 0
#define SCULL_NR_DEVS 4

int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int key_len = 5;
size_t zero_count = 0;

char *scull_key;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev
{
    char *data;
    unsigned long size;
    struct semaphore sem;
    struct cdev cdev;
};

struct scull_dev *scull_devices;

void enc_dec(char *param, size_t param_size, int is_decrypt)
{

    char iter1 = scull_key[0];
    size_t i, j;

    size_t mod = (param_size - 1) % key_len;
    zero_count = key_len - mod;
    if (mod == 0)
    {
        zero_count = 0;
    }

    krealloc(param, (param_size + zero_count) * sizeof(char), GFP_KERNEL);

    if (mod != 0)
    {
        for (i = 0; i < zero_count; i++)
        {
            param[i+param_size-1] = '0';
        }
    }

    size_t keyarr[key_len];
    for (i = 0; i < key_len; i++)
    {
        int small = 0;
        iter1 = scull_key[i];
        for (j = 0; j < key_len; j++)
        {
            char iter2 = scull_key[j];
            if (!(iter1 <= iter2))
            {
                small++;
            }
        }
        keyarr[i] = small;
    }

    char *temp;
    temp = kmalloc((param_size + zero_count) * sizeof(char), GFP_KERNEL);

    for (i = 0; i < param_size + 1 + zero_count; i = i + key_len)
    {
        for (j = 0; j < key_len; j++)
        {
            if (is_decrypt == 1)
            {
                temp[keyarr[j] + i] = param[i + j];
            }

            else
            {
                temp[j + i] = param[i + keyarr[j]];
            }
        }
    }
    temp[param_size + zero_count] = 0;

    memcpy(param, temp, param_size + zero_count);

    kfree(temp);
}

void key_init(custom_key i_key)
{
    krealloc(scull_key, (i_key.size + 1) * sizeof(char), GFP_KERNEL);
    int i;
    for (i = 0; i < i_key.size; i++)
    {
        scull_key[i] = i_key.key[i];
    }
    scull_key[i] = 0;
    key_len = i_key.size;
}

int scull_trim(struct scull_dev *dev)
{
    int i;

    if (dev->data)
    {
        kfree(dev->data);
    }
    dev->data = NULL;
    dev->size = 0;
    return 0;
}

int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
    {
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scull_trim(dev);
        up(&dev->sem);
    }
    return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
    return 0;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    ssize_t retval = 0;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    if (dev->data == NULL)
        goto out;

    char *temp_buf;
    temp_buf = kmalloc((count) * sizeof(char), GFP_KERNEL);
    memcpy(temp_buf, dev->data, count);
    enc_dec(temp_buf, count, 1);
    count += zero_count;

    if (copy_to_user(buf, temp_buf, count))
    {
        retval = -EFAULT;
        goto out;
    }
    kfree(temp_buf);

    *f_pos += count;
    retval = count;

out:
    up(&dev->sem);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *temp_buf;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (!dev->data)
    {
        dev->data = kmalloc((count) * sizeof(char *), GFP_KERNEL);
        if (!dev->data)
            goto out;
        memset(dev->data, 0, sizeof(char *));
    }

    temp_buf = kmalloc((count) * sizeof(char *), GFP_KERNEL);

    if (copy_from_user(temp_buf, buf, count))
    {
        retval = -EFAULT;
        goto out;
    }


    enc_dec(temp_buf, count, 0);

    dev->data = kmalloc((count + zero_count) * sizeof(char), GFP_KERNEL);

    count += zero_count;

    memcpy(dev->data, temp_buf, count);

    kfree(temp_buf);

    *f_pos += count;
    retval = count;


    if (dev->size < *f_pos)
        dev->size = *f_pos;

out:
    up(&dev->sem);
    return retval;
}

long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

    int err = 0;
    int retval = 0;


    if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > SCULL_IOC_MAXNR)
        return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    switch (cmd)
    {
    case SCULL_IOCRESET:

        break;
    case SCULL_CHANGE_KEY:
        if (!capable(CAP_SYS_ADMIN))
        {
            return -EPERM;
        }

        custom_key i_key;
        if (copy_from_user(&i_key, (char __user *)arg, sizeof(custom_key)))
        {
            return -EFAULT;
        }
        key_init(i_key);

        break;
    case SCULL_DATA_RESET:
        if (!capable(CAP_SYS_ADMIN))
        {
            return -EPERM;
        }
        struct scull_dev *dev = filp->private_data;
        scull_trim(dev);
        break;

    default: /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }
    return retval;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    switch (whence)
    {
    case 0: /* SEEK_SET */
        newpos = off;
        break;

    case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

    case 2: /* SEEK_END */
        newpos = dev->size + off;
        break;

    default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0)
        return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .llseek = scull_llseek,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open = scull_open,
    .release = scull_release,
};

void scull_cleanup_module(void)
{
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    if (scull_devices)
    {
        for (i = 0; i < scull_nr_devs; i++)
        {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
        kfree(scull_key);
    }

    unregister_chrdev_region(devno, scull_nr_devs);
}

int scull_init_module(void)
{
    int result, i;
    int err;
    dev_t devno = 0;
    struct scull_dev *dev;
    
    scull_key = kmalloc((key_len + 1) * sizeof(char), GFP_KERNEL);

    scull_key[0] = 'c';
    scull_key[1] = 'e';
    scull_key[2] = 'a';
    scull_key[3] = 'y';
    scull_key[4] = 'f';
    scull_key[5]  = 0;

    if (scull_major)
    {
        devno = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(devno, scull_nr_devs, "scull");
    }
    else
    {
        result = alloc_chrdev_region(&devno, scull_minor, scull_nr_devs,
                                     "scull");
        scull_major = MAJOR(devno);
    }
    if (result < 0)
    {

        return result;
    }

    scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev),
                            GFP_KERNEL);
    if (!scull_devices)
    {
        result = -ENOMEM;
        goto fail;
    }
    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    /* Initialize each device. */
    for (i = 0; i < scull_nr_devs; i++)
    {
        dev = &scull_devices[i];
        sema_init(&dev->sem, 1);
        devno = MKDEV(scull_major, scull_minor + i);
        cdev_init(&dev->cdev, &scull_fops);
        dev->cdev.owner = THIS_MODULE;
        dev->cdev.ops = &scull_fops;
        err = cdev_add(&dev->cdev, devno, 1);
        if (err)
        {
            printk(KERN_NOTICE "Error %d adding scull%d", err, i);
        }
    }
    return 0; /* succeed */

fail:
    scull_cleanup_module();
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
