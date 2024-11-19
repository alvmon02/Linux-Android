#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

MODULE_LICENSE("GPL");

/*
 *  Prototypes
 */
int init_module(void);
void cleanup_module(void);
static int prodcons_open(struct inode*, struct file*);
static int prodcons_release(struct inode*, struct file*);
static ssize_t prodcons_read(struct file* filp, char __user* buf, size_t len, loff_t* off);
static ssize_t prodcons_write(struct file* filp, const char __user* buff, size_t len, loff_t* off);

#define DEVICE_NAME "prodcons"  /* Dev name as it appears in /proc/devices   */
#define CLASS_NAME "mprodcons"

#define MAX_CHARS_KBUF 4
struct kfifo mKfifo;
struct semaphore mtx, huecos, elementos;

static struct file_operations fops = {
    .read = prodcons_read,
    .write = prodcons_write,
    .open = prodcons_open,
    .release = prodcons_release,
    .owner = THIS_MODULE
};


static struct miscdevice misc_prodcons = {
    .minor = MISC_DYNAMIC_MINOR,    /* kernel dynamically assigns a free minor# */
    .name = DEVICE_NAME, /* when misc_register() is invoked, the kernel
                        * will auto-create device file as /dev/chardev ;
                        * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
    .mode = 0666,     /* ... dev node perms set as specified here */
    .fops = &fops,    /* connect to this driver's 'functionality' */
};

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
    int major;      /* Major number assigned to our device driver */
    int minor;      /* Minor number assigned to the associated character device */
    int ret;
    struct device* device;

    ret = misc_register(&misc_prodcons);

    if (ret) {
        pr_err("Couldn't register misc device\n");
        return ret;
    }

    device = misc_prodcons.this_device;

    /* Access devt field in device structure to retrieve (major,minor) */
    major = MAJOR(device->devt);
    minor = MINOR(device->devt);

    dev_info(device, "I was assigned major number %d. To talk to\n", major);
    dev_info(device, "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
    dev_info(device, "Remove the module when done.\n");

    try_module_get(THIS_MODULE);

    if (kfifo_alloc(&mKfifo, MAX_CHARS_KBUF * sizeof(int), GFP_KERNEL))
        return -ENOMEM;

    sema_init(&mtx, 1);
    sema_init(&huecos, MAX_CHARS_KBUF);
    sema_init(&elementos, 0);

    return 0;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
    if (down_interruptible(&mtx))
        kfifo_free(&mKfifo);
    

    misc_deregister(&misc_prodcons);
    pr_info("prodcons misc driver deregistered. Bye\n");
}

/*
 * Called when a process tries to open the device file.
 */
static int prodcons_open(struct inode* inode, struct file* file)
{
    return 0;
}

/*
 * Called when a process closes the device file.
 */
static int prodcons_release(struct inode* inode, struct file* file)
{
    module_put(THIS_MODULE);

    return 0;
}

/*
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t prodcons_read(struct file* filp, char __user* buf, size_t len, loff_t* off) {
    char kbuf[MAX_CHARS_KBUF + 1];
    int nr_bytes = 0;
    int val;

    if ((*off) > 0)
        return 0;

    if (down_interruptible(&elementos))
        return -EINTR;

    /* Entrar a la SC */
    if (down_interruptible(&mtx)) {
        up(&elementos);
        return -EINTR;
    }

    /* Extraer el primer entero del buffer */
    kfifo_out(&kbuf, &val, sizeof(int));
    /* Salir de la SC */
    up(&mtx);
    up(&huecos);

    nr_bytes = sprintf(kbuf, "%i\n", val);
    //... copy_to_user() ...
    if (copy_to_user(buf, kbuf, nr_bytes))
        return -EFAULT;

    return nr_bytes;
}

static ssize_t prodcons_write(struct file* filp, const char __user* buff, size_t len, loff_t* off) {
    char kbuf[MAX_CHARS_KBUF + 1];
    int val = 0;

    // .. copy_from_user() + Convertir char* a entero y almacenarlo en val ..
    if (copy_from_user(kbuf, buff, len))
        return -EFAULT;

    kbuf[len] = '\0';

    if (strlen(kbuf) > 2)
        return -EINVAL;

    if (sscanf(kbuf, "%i,", &val) != 1)
        return -EINVAL;

    if (down_interruptible(&huecos))
        return -EINTR;

    /* Entrar a la SC */
    if (down_interruptible(&mtx)) {
        up(&huecos);
        return -EINTR;
    }

    /* Inserci�n en el buffer circular */
    kfifo_in(&mKfifo, &val, sizeof(int));

    /* Salir de la SC */
    up(&mtx);
    up(&elementos);

    return len;
}
