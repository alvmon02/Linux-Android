#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

/* Bits associated with each segment */
#define DS_A 0x80
#define DS_B 0x40
#define DS_C 0x20
#define DS_D 0x10
#define DS_E 0x08
#define DS_F 0x04
#define DS_G 0x02
#define DS_DP 0x01
#define SEGMENT_COUNT 8

/**
 * Number Representation in Segments form,
 * aided by usage of Bits Macros
 */

#define N0 DS_A|DS_B|DS_C|DS_D|DS_E|DS_F
#define N1 DS_B|DS_C
#define N2 DS_A|DS_B|DS_G|DS_D|DS_E
#define N3 DS_A|DS_B|DS_C|DS_D|DS_G
#define N4 DS_B|DS_C|DS_F|DS_G
#define N5 DS_A|DS_F|DS_G|DS_C|DS_D
#define N6 DS_A|DS_C|DS_D|DS_E|DS_F|DS_G
#define N7 DS_A|DS_B|DS_C
#define N8 DS_A|DS_B|DS_C|DS_D|DS_E|DS_F|DS_G
#define N9 DS_A|DS_B|DS_C|DS_F|DS_G
#define NA DS_A|DS_B|DS_C|DS_E|DS_F|DS_G
#define NB DS_C|DS_D|DS_E|DS_F|DS_G
#define NC DS_A|DS_D|DS_E|DS_F
#define ND DS_B|DS_C|DS_D|DS_E|DS_G
#define NE DS_A|DS_D|DS_E|DS_F|DS_G
#define NF DS_A|DS_E|DS_F|DS_G
#define NCOUNT 16

#define MAX_LEN 128

/* Indices of GPIOs used by this module */
enum
{
	SDI_IDX = 0,
	RCLK_IDX,
	SRCLK_IDX,
	NR_GPIO_DISPLAY
};

/* Pin numbers */
const int display_gpio[NR_GPIO_DISPLAY] = {18, 23, 24};

/* Array to hold GPIO descriptors */
struct gpio_desc *gpio_descriptors[NR_GPIO_DISPLAY];

const char *display_gpio_str[NR_GPIO_DISPLAY] = {"sdi", "rclk", "srclk"};

/* Sequence of segments used by the character device driver */
const int sequence[] = {DS_D, DS_E, DS_F, DS_A, DS_B, DS_C, DS_G, DS_DP, -1};

//Lista de todas las representaciones de números en 7segment(tamaño=16)
const int sequenceN[] = {N0, N1, N2,N3, N4, N5, N6, N7, N8, N9, NA, NB, NC, ND, NE, NF};
#define DEVICE_NAME "display7s" /* Device name */

/*
 *  Prototypes
 */
static ssize_t display7s_write(struct file *, const char *, size_t, loff_t *);

/* Simple initialization of file_operations interface with a single operation */
static struct file_operations fops = {
	.write = display7s_write,
};

static struct miscdevice display7s_misc = {
	.minor = MISC_DYNAMIC_MINOR, /* kernel dynamically assigns a free minor# */
	.name = DEVICE_NAME,		 /* when misc_register() is invoked, the kernel
								  * will auto-create device file;
								  * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
	.mode = 0666,				 /* ... dev node perms set as specified here */
	.fops = &fops,				 /* connect to this driver's 'functionality' */
};


/* Update the 7-segment display with the configuration specified by the data parameter */
static void update_7sdisplay(unsigned char data)
{
	int i = 0;
	int value = 0;

	for (i = 0; i < SEGMENT_COUNT; i++)
	{
		/* Explore current bit (from most significant to least significant) */
		if (0x80 & (data << i))
			value = 1;
		else
			value = 0;

		/* Set value of serial input */
		gpiod_set_value(gpio_descriptors[SDI_IDX], value);
		/* Generate clock cycle in shift register */
		gpiod_set_value(gpio_descriptors[SRCLK_IDX], 1);
		msleep(1);
		gpiod_set_value(gpio_descriptors[SRCLK_IDX], 0);
	}

	/* Generate clock cycle in output register to update 7-seg display */
	gpiod_set_value(gpio_descriptors[RCLK_IDX], 1);
	msleep(1);
	gpiod_set_value(gpio_descriptors[RCLK_IDX], 0);
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/display7s
 * Test with the following command to see the sequence better:
 * $ while true; do echo > /dev/display7s; sleep 0.3; done
 */
static ssize_t
display7s_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	int hex_Num = 0;
	char c[MAX_LEN + 1];

	if (len > MAX_LEN)
		return -1;

	if (copy_from_user(c, buff, len))
		return -EFAULT;

	c[len] = '\0';

	if(strlen(c) > 2)
		return -EINVAL;

	if(sscanf(c, "%x,", &hex_Num) != 1)
		return -EINVAL;

	if(hex_Num < 0 || hex_Num > 15)
		return -EINVAL;

	/* Update the corresponding value in the display */
	update_7sdisplay(sequenceN[hex_Num]);

	return len;
}

static int __init display7s_misc_init(void)
{
	int i, j;
	int err = 0;
	struct device *device;

	for (i = 0; i < NR_GPIO_DISPLAY; i++)
	{

		/* Request the GPIO */
		if ((err = gpio_request(display_gpio[i], display_gpio_str[i])))
		{
			pr_err("Failed GPIO[%d] %d request\n", i, display_gpio[i]);
			goto err_handle;
		}

		/* Transform number into descriptor */
		if (!(gpio_descriptors[i] = gpio_to_desc(display_gpio[i])))
		{
			pr_err("GPIO[%d] %d is not valid\n", i, display_gpio[i]);
			err = -EINVAL;
			goto err_handle;
		}

		/* Configure as an output pin */
		gpiod_direction_output(gpio_descriptors[i], 0);
	}

	/* Set everything as LOW */
	for (i = 0; i < NR_GPIO_DISPLAY; i++)
		gpiod_set_value(gpio_descriptors[i], 0);

	/* Register misc device that exposes 7-segment display */
	err = misc_register(&display7s_misc);

	if (err)
	{
		pr_err("Couldn't register misc device\n");
		goto err_handle;
	}

	device = display7s_misc.this_device;

	dev_info(device, "Display7s driver registered succesfully. To talk to\n");
	dev_info(device, "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
	dev_info(device, "Remove the module when done.\n");

	return 0;
err_handle:
	for (j = 0; j < i; j++)
		gpiod_put(gpio_descriptors[j]);
	return err;
}

static void __exit display7s_misc_exit(void)
{
	int i = 0;

	/* Unregister character device */
	misc_deregister(&display7s_misc);

	/* Clear display */
	update_7sdisplay(0);

	/* Free up pins */
	for (i = 0; i < NR_GPIO_DISPLAY; i++)
		gpiod_put(gpio_descriptors[i]);

	pr_info("Display7s driver deregistered. Bye\n");
}

module_init(display7s_misc_init);
module_exit(display7s_misc_exit);
