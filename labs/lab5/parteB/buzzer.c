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
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include <asm/atomic.h>

MODULE_DESCRIPTION("Buzzer Kernel Module - FDI-UCM");
MODULE_LICENSE("GPL");

#define PWM_DEVICE_NAME "pwmchip0"
#define BUZZER_DEVICE_NAME "buzzer"

#define MAX_LEN 256
#define WORK_SIZE (sizeof(struct work_struct))
#define MUSIC_SIZE (sizeof(struct music_step))

/* Structure to represent a note or rest in a melodic line  */
struct music_step
{
	unsigned int freq : 24; /* Frequency in centihertz */
	unsigned int len : 8;	/* Duration of the note */
};

typedef enum {
	BUZZER_STOPPED, /* Buzzer no reproduce nada (la melodía terminó o no ha comenzado) */
	BUZZER_PAUSED, /* Reproducción pausada por el usuario */
	BUZZER_PLAYING /* Buzzer reproduce actualmente la melodía */
} buzzer_state_t;

typedef enum {
	REQUEST_START, /* Usuario pulsó SW1 durante estado BUZZER_STOPPED */
	REQUEST_RESUME, /* Usuario pulsó SW1 durante estado BUZZER_PAUSED */
	REQUEST_PAUSE, /* Usuario pulsó SW1 durante estado BUZZER_PLAYING */
	REQUEST_CONFIG, /* Usuario está configurando actualmente una nueva melodía vía /dev/buzzer */
	REQUEST_NONE /* Indicador de petición ya gestionada (a establecer por tarea diferida) */
} buzzer_request_t;

//static struct workqueue_struct *my_workqueue;
//Workqueues Descriptors
struct workqueue_struct *my_workqueue; /* Workqueue Descriptor */
struct work_struct *my_work_list,/* Work descriptor */
				   *next_work;/* Work descriptor */

//Buzzer Descriptions
struct pwm_device *pwm_device = NULL;
struct pwm_state pwm_state;

static spinlock_t lock; /* Cerrojo para proteger actualización/consulta de variables buzzer_state y buzzer_request */
static buzzer_state_t buzzer_state=BUZZER_STOPPED; /* Estado actual de la reproducción */
static buzzer_request_t buzzer_request=REQUEST_NONE;

struct music_step *melody = NULL,
				  *next_note = NULL;/* Puntero a la siguiente nota de la melodía actual (solo alterado por tarea diferida) */
int played_notes=0, melody_notes=0;
int bpm = 100;

struct timer_list buzzer_timer;	//Estrucutura para definir el Kernel Timer

// Preprocessors(functions/procedures)
static int __init pwm_module_init(void);
static inline unsigned int freq_to_period_ns(unsigned int frequency);
static inline int is_end_marker(struct music_step *step);
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref);
static void my_wq_function(struct work_struct *work);
static void __exit pwm_module_exit(void);
static int buzzer_open(struct inode *inode, struct file *file);
static ssize_t buzzer_write(struct file *filp, const char *buff, size_t len, loff_t *off);
static ssize_t buzzer_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static int buzzer_release(struct inode *inode, struct file *file);
static void timer_signal_function(struct timer_list *timer);

/* Simple initialization of file_operations interface with a single operation */
static struct file_operations fops = {
	.write = buzzer_write,
	.read = buzzer_read,
	.open = buzzer_open,
	.release = buzzer_release
};

static struct miscdevice buzzer_misc = {
	.minor = MISC_DYNAMIC_MINOR, /* kernel dynamically assigns a free minor# */
	.name = BUZZER_DEVICE_NAME,	 /* when misc_register() is invoked, the kernel
								  * will auto-create device file;
								  * also populated within /sys/class/misc/ and /sys/devices/virtual/misc/ */
	.mode = 0666,				 /* ... dev node perms set as specified here */
	.fops = &fops,				 /* connect to this driver's 'functionality' */
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int __init pwm_module_init(void)
{
	int err = 0;
	struct device *device;

	/* Request utilization of PWM0 device */
	pwm_device = pwm_request(0, PWM_DEVICE_NAME);

	if (IS_ERR(pwm_device))
		return PTR_ERR(pwm_device);

	err = misc_register(&buzzer_misc);
	my_workqueue = create_workqueue("notes_queue");



	if(my_workqueue)
	{
		if (err) {
			pr_err("Couldn't register misc device\n");
			goto err_handle;
		}
	
		device = buzzer_misc.this_device;
	
		//Asignación de memoria para melodía y siguiente nota
		melody = vmalloc(PAGE_SIZE);
		next_note = melody;
		melody_notes = 0;
		//next_note = kmalloc(sizeof(struct music_step),GFP_KERNEL);

		//Creación del timer
		timer_setup(&buzzer_timer, timer_signal_function, 0);
		//buzzer_time.expires = jiffies + (HZ * 1);	//Comenzar el Timer un segundo después de carga(Posiblemente innecesario)

		//Activar el timer por primera vez(A lo mejor innecesario?)
		add_timer(&buzzer_timer);

		dev_info(device, "Buzzer driver registered succesfully. To talk to\n");
		dev_info(device, "the driver try to cat and echo to /dev/%s.\n", BUZZER_DEVICE_NAME);
		dev_info(device, "Remove the module when done.\n");
	}
	
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

/*
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t buzzer_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	char c[MAX_LEN + 1];
	int nr_bytes = 0;

	if((*off) > 0)
		return 0;

	nr_bytes = sprintf(c,"beat=%i\n",bpm);

	if (copy_to_user(buf, c, nr_bytes))
		return -EFAULT;

	*off += nr_bytes;

	return nr_bytes;
}

/**
 * Called when a process writes to dev file
 */
static ssize_t buzzer_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	char c[MAX_LEN + 1],
		*c1,
		*c2 = NULL;

	int beat = 0,
		freq = 0,
		len_note = 0,
		i = 0;

	if (copy_from_user(c, buff, len))
		return -EFAULT;

	c[len] = '\0';

	c2 = kmalloc(MAX_LEN + 1,GFP_KERNEL);

	if(sscanf(c, "music %s", c2) == 1)
	{
		melody_notes = 0;
		printk(KERN_INFO "Music Reading\n");

		c1 = strsep(&c2,",");
		while(c1 != NULL)
		{
			printk(KERN_INFO "Element: %s\n",c1);

			sscanf(c1,"%i:%x", &freq, &len_note);
			melody[melody_notes].freq = freq;
			melody[melody_notes].len = len_note;

			//printk(KERN_INFO "Nota: %i\n",melody[notes].freq);
			//printk(KERN_INFO "Tiempo: %i\n",melody[notes].len);

			c1 = strsep(&c2,",");

			melody_notes++;
		}
	}
	else if(sscanf(c, "beat %i", &beat) == 1)
		bpm = beat;
	else if(sscanf(c, "start %i", &beat) == 1)//ONLY FOR EXPERIMENT, MUST CHANGE TO BUTTON
	{
		my_work_list = kmalloc(WORK_SIZE, GFP_KERNEL);
		if(my_work_list)
		{
			next_note = melody;

			//i = 0;
			next_work = my_work_list;
			//printk(KERN_INFO "Cantidad de Notes %i \n",notes);

			//while(i < notes && next_work != NULL){
			INIT_WORK(next_work, my_wq_function);

			/* Enqueue work */
			schedule_work(next_work);

				//i++;
				//if(i >= notes)
					//next_work = NULL;
				//else
				//	next_work = my_work_list + (i * WORK_SIZE);

				//}
		}
	}

	kfree(c2);

	return len;
}



/*
 * Called when a process closes the device file.
 */
static int buzzer_release(struct inode *inode, struct file *file)
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
	flush_workqueue(my_workqueue);
	destroy_workqueue(my_workqueue);

	kfree(my_work_list);

	/* Wait until completion of the timer function (if it's currently running) and delete timer */
	del_timer_sync(&buzzer_timer);

	/* Release PWM device */
	pwm_free(pwm_device);

	misc_deregister(&buzzer_misc);

	vfree(melody);

	//kfree(next_note);

	printk(KERN_INFO "Module extracted Successfully\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* Work's handler function */


//Reproducción de nota(primera) con configuración de pwm_buzzer
static void my_wq_function(struct work_struct *work)
{
	struct music_step *next;

	next_note = melody + (MUSIC_SIZE * played_notes);

	if(next_note != NULL && played_notes != melody_notes)
		next = next_note;
	else
		return;

	pwm_init_state(pwm_device, &pwm_state);

	/* Obtain period from frequency */
	pwm_state.period = freq_to_period_ns(next->freq);

	/**
	 * Disable temporarily to allow repeating the same consecutive
	 * notes in the melodic line
	 **/
	//pwm_disable(pwm_device);

	/* If period==0, its a rest (silent note) */
	if (pwm_state.period > 0)
	{
		/* Set duty cycle to 70 to maintain the same timbre */
		pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
		pwm_state.enabled = true;
		/* Apply state */
		pwm_apply_state(pwm_device, &pwm_state);
	}
	else {	/* Disable for rest */
		pwm_disable(pwm_device);
	}

	played_notes++;

	//MODTIMER que expira cuando la duración de la nota finaliza
	mod_timer(&buzzer_timer,jiffies + msecs_to_jiffies(calculate_delay_ms(next->len, bpm)));
}

static void timer_signal_function(struct timer_list *timer)
{
	if(buzzer_state == BUZZER_PAUSED)
		pwm_disable(pwm_device);
	else
	{
		INIT_WORK(next_work, my_wq_function);

		/* Enqueue work */
		schedule_work(next_work);
	}

}

static void create_workqueue_from_melody(void)
{
	int i = 0;

	if(melody_notes == 0)
		return;

	my_work_list = kmalloc(WORK_SIZE * melody_notes, GFP_KERNEL);

	if(my_work_list)
	{
		pwm_init_state(pwm_device, &pwm_state);

		next_note = melody;

		i = 0;
		next_work = my_work_list;
		printk(KERN_INFO "Cantidad de Notes %i \n",melody_notes);

		while(i < melody_notes && next_work != NULL)
		{
			INIT_WORK(next_work, my_wq_function);

			/* Enqueue work */
			schedule_work(next_work);

			i++;
			if(i >= melody_notes)
				next_work = NULL;
			else
				next_work = my_work_list + (i * WORK_SIZE);

		}
	}

}
//Función de pulsador que ejecutará la primera(única?) tarea diferida
static void pulsar_button(void)
{
	create_workqueue_from_melody();

	schedule_work(next_work);
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
