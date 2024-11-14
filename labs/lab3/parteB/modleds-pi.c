#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/uaccess.h>  /* for copy_to_user */

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0
#define LED1 0b100
#define LED2 0b010
#define LED3 0b001

#define NR_GPIO_LEDS  3
#define MAX_LEN 128

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,1)
#define __cconst__ const
#else
#define __cconst__ 
#endif

MODULE_LICENSE("GPL");

/* Actual GPIOs used for controlling LEDs */
const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};

/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_LEDS];
static inline int set_pi_leds(unsigned int mask);

static dev_t start;
static struct cdev* modleds = NULL;
static struct class* class = NULL;
static struct device* device = NULL;

#define DEVICE_NAME "leds"
#define CLASS_NAME "mleds"

static ssize_t modleds_write(struct file *filp, const char __user *buff, size_t len, loff_t *off) {
  int num = 0,real_Leds = 0;
	char c[MAX_LEN + 1];

	if (len > MAX_LEN)
		return -1;

	if (copy_from_user(c, buff, len))
		return -EFAULT;

	c[len] = '\0';

	if(strlen(c) > 2)
		return -EINVAL;

	if(sscanf(c, "%i,", &num) != 1)
		return -EINVAL;

	if(num < 0 || num > 7)
		return -EINVAL;

  if(num & LED3)
		real_Leds |= LED1;
	
	if(num & LED2)
		real_Leds |= LED2;
	
	if(num & LED1)
		real_Leds |= LED3;

  set_pi_leds(real_Leds);

  return len;
}

static ssize_t modleds_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    return -EPERM;
}

static struct file_operations fops = {
  .read = modleds_read,
  .write = modleds_write,
};

/* Set led state to that specified by mask */
static inline int set_pi_leds(unsigned int mask) {
  int i;
  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1 );
  return 0;
}

static char *custom_devnode(__cconst__ struct device *dev, umode_t *mode)
{
  if (!mode)
    return NULL;
  if (MAJOR(dev->devt) == MAJOR(start))
    *mode = 0666;
  return NULL;
}

static int __init modleds_init(void)
{
  int i, j;
  char gpio_str[10];

  int major;    /* Major number assigned to our device driver */
  int minor;    /* Minor number assigned to the associated character device */
  int ret = 0;

   /* Get available (major,minor) range */
  if ((ret = alloc_chrdev_region (&start, 0, 1, DEVICE_NAME))) {
    printk(KERN_INFO "Can't allocate chrdev_region()");
    goto error_alloc_region;
  }

  /* Create associated cdev */
  if ((modleds = cdev_alloc()) == NULL) {
    printk(KERN_INFO "cdev_alloc() failed ");
    ret = -ENOMEM;
    goto error_alloc;
  }

  cdev_init(modleds, &fops);

  if ((ret = cdev_add(modleds, start, 1))) {
    printk(KERN_INFO "cdev_add() failed ");
    goto error_add;
  }

  /* Create custom class */
  class = class_create(THIS_MODULE, CLASS_NAME);

  if (IS_ERR(class)) {
    pr_err("class_create() failed \n");
    ret = PTR_ERR(class);
    goto error_class;
  }

  /* Establish function that will take care of setting up permissions for device file */
  class->devnode = custom_devnode;

  /* Creating device */
  device = device_create(class, NULL, start, NULL, DEVICE_NAME);

  if (IS_ERR(device)) {
    pr_err("Device_create failed\n");
    ret = PTR_ERR(device);
    goto error_device;
  }

  major = MAJOR(start);
  minor = MINOR(start);

  printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
  printk(KERN_INFO "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
  printk(KERN_INFO "Remove the module when done.\n");

  printk(KERN_INFO "modleds: Module loaded.\n");

  for (i = 0; i < NR_GPIO_LEDS; i++) {
    /* Build string ID */
    sprintf(gpio_str, "led_%d", i);
    /* Request GPIO */
    if ((ret = gpio_request(led_gpio[i], gpio_str))) {
      pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
      goto error_handle;
    }

    /* Transforming into descriptor */
    if (!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i]))) {
      pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
      ret = -EINVAL;
      goto error_handle;
    }

    gpiod_direction_output(gpio_descriptors[i], 0);
  }

  return 0;
error_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);
error_device:
  class_destroy(class);
error_class:
  /* Destroy chardev */
  if (modleds) {
    cdev_del(modleds);
    modleds = NULL;
  }
error_add:
  /* Destroy partially initialized chardev */
  if (modleds)
    kobject_put(&modleds->kobj);
error_alloc:
  unregister_chrdev_region(start, 1);
error_alloc_region:

  return ret;
}

static void __exit modleds_exit(void) {
  int i = 0;

  set_pi_leds(ALL_LEDS_OFF);

  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_put(gpio_descriptors[i]);

   if (device)
    device_destroy(class, device->devt);

  if (class)
    class_destroy(class);

  /* Destroy chardev */
  if (modleds)
    cdev_del(modleds);

  /*
   * Release major minor pair
   */
  unregister_chrdev_region(start, 1);

  printk(KERN_INFO "Modleds_ParteB: Module unloaded.\n");

}

module_init(modleds_init);
module_exit(modleds_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
