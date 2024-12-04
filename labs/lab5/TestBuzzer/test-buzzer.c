#include <asm-generic/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/tty.h> /* For fg_console */
#include <linux/kd.h>  /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

MODULE_DESCRIPTION("Buzzer Kernel Module - FDI-UCM");
// MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

/* Frequency of selected notes in centihertz */
#define C4 26163
#define D4 29366
#define E4 32963
#define F4 34923
#define G4 39200
#define C5 52325

#define PWM_DEVICE_NAME "pwmchip0"
#define BUZZER_DEVICE_NAME "buzzer"

struct pwm_device *pwm_device = NULL;
struct pwm_state pwm_state;

/* Work descriptor */
struct work_struct my_work;

/* Structure to represent a note or rest in a melodic line  */
struct music_step
{
	unsigned int freq : 24; /* Frequency in centihertz */
	unsigned int len : 8;	/* Duration of the note */
};

// Preprocessors(function/procedures)
static int __init pwm_module_init(void);
static inline unsigned int freq_to_period_ns(unsigned int frequency);
static inline int is_end_marker(struct music_step *step);
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref);
static void my_wq_function(struct work_struct *work);
static void __exit pwm_module_exit(void);

// static int display7s_open(struct inode *inode, struct file *file);
static int buzzer_open(struct inode *inode, struct file *file);
static ssize_t buzzer_write(struct file *filp, const char *buff, size_t len, loff_t *off);
static int buffer_release(struct inode *inode, struct file *file);
// static int display7s_release(struct inode *inode, struct file *file);

/* Simple initialization of file_operations interface with a single operation */
static struct file_operations fops = {
	.write = buzzer_write,
	//.open = display7s_open,
	//.release = display7s_release
};

static struct miscdevice buzzer_misc = {
	.minor = MISC_DYNAMIC_MINOR, /* kernel dynamically assigns a free minor# */
	.name = BUZZER_DEVICE_NAME,	 /* when misc_register() is invoked, the kernel
								  * will auto-create device file;
								  * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
	.mode = 0666,				 /* ... dev node perms set as specified here */
	.fops = &fops,				 /* connect to this driver's 'functionality' */
};

static int __init pwm_module_init(void)
{
	int err = 0;
	struct device *device;

	/* Request utilization of PWM0 device */
	pwm_device = pwm_request(0, PWM_DEVICE_NAME);

	if (IS_ERR(pwm_device))
		return PTR_ERR(pwm_device);

	err = misc_register(&buzzer_misc);

	if (err)
	{
		pr_err("Couldn't register misc device\n");
		goto err_handle;
	}

	device = buzzer_misc.this_device;

	dev_info(device, "Buzzer driver registered succesfully. To talk to\n");
	dev_info(device, "the driver try to cat and echo to /dev/%s.\n", BUZZER_DEVICE_NAME);
	dev_info(device, "Remove the module when done.\n");

	return 0;
err_handle:
	return err;
}

/*
 * Called when a process tries to open the device file, like
 * "cat /dev/buzzer"
 */
static int buzzer_open(struct inode *inode, struct file *file)
{
	/*
	mutex_lock(&openDev);
	if (Open_Dev){
		mutex_unlock(&openDev);

		return -EBUSY;
	}
	Open_Dev++;
	mutex_unlock(&openDev);
	*/

	/* Increment the module's reference counter */
	try_module_get(THIS_MODULE);
	return 0;
}

/**
 * Called when a process writes to dev file
 */
static ssize_t buzzer_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	char c[MAX_LEN + 1];
	int element_Size=0;
	int aux;

	if (copy_from_user(c, user_buffer, len))
		return -EFAULT;

	c[len] = '\0';
	while (strsep(c,',') != NULL)
	{
		if(sscanf(c, "%x,", &aux) != 1)
			return -EINVAL;
	}
	
	
	/**
	 *
// Matriz de mensajes (64 bytes) con la información de cada LED
Copiar cadena alojada en user_buffer a buffer auxiliar (kbuf). No olvidar incluir el terminador ('\0')..
Hacer el parsing de la cadena en kbuf:
- Partir en tokens separados con ',' con strsep()
- Analizar el contenido de cada par (ledn,color) con sscanf()
- Rellenar el mensaje correspondiente para el LED en cuestión en messages
Enviar los mensajes en messages (uno a uno) al dispositivo con usb_control_msg()
Actualizar puntero de posición de fichero y retornar valor adecuado
}
	 */

	INIT_WORK(&my_work, my_wq_function);

	/* Enqueue work */
	schedule_work(&my_work);

	return len;
}

/* Work's handler function */
static void my_wq_function(struct work_struct *work)
{
	struct music_step melodic_line[] = {
		{C4, 4}, {E4, 4}, {G4, 4}, {C5, 4}, {0, 2}, {C5, 4}, {G4, 4}, {E4, 4}, {C4, 4}, {0, 0} /* Terminator */
	};
	const int beat = 120; /* 120 quarter notes per minute */
	struct music_step *next;

	pwm_init_state(pwm_device, &pwm_state);

	/* Play notes sequentially until end marker is found */
	for (next = melodic_line; !is_end_marker(next); next++)
	{
		/* Obtain period from frequency */
		pwm_state.period = freq_to_period_ns(next->freq);

		/**
		 * Disable temporarily to allow repeating the same consecutive
		 * notes in the melodic line
		 **/
		pwm_disable(pwm_device);

		/* If period==0, its a rest (silent note) */
		if (pwm_state.period > 0)
		{
			/* Set duty cycle to 70 to maintain the same timbre */
			pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
			pwm_state.enabled = true;
			/* Apply state */
			pwm_apply_state(pwm_device, &pwm_state);
		}
		else
		{
			/* Disable for rest */
			pwm_disable(pwm_device);
		}

		/* Wait for duration of the note or reset */
		msleep(calculate_delay_ms(next->len, beat));
	}

	pwm_disable(pwm_device);
}

/*
 * Called when a process closes the device file.
 */
static int buffer_release(struct inode *inode, struct file *file)
{
	/*
	mutex_lock(&openDev);
	Open_Dev--;
	mutex_unlock(&openDev);
	*/
	module_put(THIS_MODULE);

	return 0;
}

static void __exit pwm_module_exit(void)
{
	/* Wait until defferred work has finished */
	flush_work(&my_work);

	/* Release PWM device */
	pwm_free(pwm_device);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Función auxiliar de tiempo
 * Transform frequency in centiHZ into period in nanoseconds */
static inline unsigned int freq_to_period_ns(unsigned int frequency)
{
	if (frequency == 0)
		return 0;
	else
		return DIV_ROUND_CLOSEST_ULL(100000000000UL, frequency);
}

/**
 * Función auxiliar de tiempo
 * Transform note length into ms,
 * taking the beat of a quarter note as reference
 */
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref)
{
	unsigned char duration = (note_len & 0x7f);
	unsigned char triplet = (note_len & 0x80);
	unsigned char i = 0;
	unsigned char current_duration;
	int total = 0;

	/* Calculate the total duration of the note
	 * as the summation of the figures that make
	 * up this note (bits 0-6)
	 */
	while (duration)
	{
		current_duration = (duration) & (1 << i);

		if (current_duration)
		{
			/* Scale note accordingly */
			if (triplet)
				current_duration = (current_duration * 3) / 2;
			/*
			 * 24000/qnote_ref denote number of ms associated
			 * with a whole note (redonda)
			 */
			total += (240000) / (qnote_ref * current_duration);
			/* Clear bit */
			duration &= ~(1 << i);
		}
		i++;
	}
	return total;
}

/**
 * Función auxiliar de control
 * Check if the current step is and end marker
 */
static inline int is_end_marker(struct music_step *step)
{
	return (step->freq == 0 && step->len == 0);
}

module_init(pwm_module_init);
module_exit(pwm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PWM test");
