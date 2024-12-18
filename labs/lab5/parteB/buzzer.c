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
#include <linux/interrupt.h>
#include <asm/atomic.h>

MODULE_DESCRIPTION("Buzzer Kernel Module - FDI-UCM");
MODULE_LICENSE("GPL");

#define PWM_DEVICE_NAME "pwmchip0"
#define BUZZER_DEVICE_NAME "buzzer"

#define MAX_LEN 2048
#define MAX_LEN_READ 128
#define WORK_SIZE (sizeof(struct work_struct))
#define MUSIC_SIZE (sizeof(struct music_step))

#define MANUAL_DEBOUNCE
#define GPIO_BUTTON	22

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
struct work_struct my_work;/* Work descriptor */

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

struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;

DEFINE_SPINLOCK(sp);

// Preprocessors(functions/procedures)

static int __init pwm_module_init(void);
static int buzzer_open(struct inode *inode, struct file *file);
static ssize_t buzzer_write(struct file *filp, const char *buff, size_t len, loff_t *off);
static ssize_t buzzer_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static int buzzer_release(struct inode *inode, struct file *file);
static void __exit pwm_module_exit(void);

static void my_wq_function(struct work_struct *work);
static void timer_signal_function(struct timer_list *timer);
static irqreturn_t gpio_irq_handler(int irq, void *dev_id);

static void load_default_melody(void);
static inline unsigned int freq_to_period_ns(unsigned int frequency);
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref);
static inline int is_end_marker(struct music_step *step);

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
	unsigned char gpio_out_ok = 0;

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
		next_note = NULL;
		melody_notes = 0;
		
		load_default_melody();

			//Creación del timer
		timer_setup(&buzzer_timer, timer_signal_function, 0);
	
		/* Requesting Button's GPIO */
		if ((err = gpio_request(GPIO_BUTTON, "button"))) {
			pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
			goto err_handle;
		}

		/* Configure Button */
		if (!(desc_button = gpio_to_desc(GPIO_BUTTON))) {
			pr_err("GPIO %d is not valid\n", GPIO_BUTTON);
			err = -EINVAL;
			goto err_handle;
		}

		gpio_out_ok = 1;

			//configure the BUTTON GPIO as input
		gpiod_direction_input(desc_button);

		/*
		** The lines below are commented because gpiod_set_debounce is not supported
		** in the Raspberry pi. Debounce is handled manually in this driver.
		*/

		#ifndef MANUAL_DEBOUNCE
			//Debounce the button with a delay of 200ms
		if (gpiod_set_debounce(desc_button, 200) < 0) {
			pr_err("ERROR: gpio_set_debounce - %d\n", GPIO_BUTTON);
			goto err_handle;
		}
		#endif

		//Get the IRQ number for our GPIO
		gpio_button_irqn = gpiod_to_irq(desc_button);

		pr_info("IRQ Number = %d\n", gpio_button_irqn);

		if (request_irq(gpio_button_irqn,		//IRQ number
						gpio_irq_handler,		//IRQ handler
						IRQF_TRIGGER_RISING,	//Handler will be called in raising edge
						"button_player",		//used to identify the device name using this IRQ
						NULL)) {				//device id for shared IRQ
			pr_err("my_device: cannot register IRQ ");
			goto err_handle;
		}

		buzzer_state = BUZZER_STOPPED;
		buzzer_request = REQUEST_NONE;

		INIT_WORK(&my_work, my_wq_function);

		dev_info(device, "Buzzer driver registered succesfully. To talk to\n");
		dev_info(device, "the driver try to cat and echo to /dev/%s.\n", BUZZER_DEVICE_NAME);
		dev_info(device, "Remove the module when done.\n");

	}
	
	return 0;
	
err_handle:
  if (gpio_out_ok)
    gpiod_put(desc_button);

	return err;

}

/*
 * Called when a process tries to open the device file, like
 * "cat /dev/buzzer"
 */
static int buzzer_open(struct inode *inode, struct file *file){
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
	char c[MAX_LEN_READ + 1];
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
	char *c = NULL,
		 *c1,
		 *c2 = NULL;
	unsigned long int flags = 0;
	int beat = 0,
		freq = 0,
		len_note = 0;

	c = kmalloc(MAX_LEN + 1,GFP_KERNEL);


	if ((c != NULL) && copy_from_user(c, buff, len))
		return -EFAULT;

	c[len] = '\0';


	c2 = kmalloc(MAX_LEN + 1,GFP_KERNEL);

	if(sscanf(c, "music %s", c2) == 1)
	{
		spin_lock_irqsave(&lock, flags);

		if(buzzer_state == BUZZER_PLAYING){
			spin_unlock_irqrestore(&lock, flags);
			kfree(c);
			kfree(c2);
			return -EBUSY;
		}


		buzzer_request = REQUEST_CONFIG;
		melody_notes = 0;
		//printk(KERN_INFO "Music Reading\n");

		c1 = strsep(&c2,",");
		while(c1 != NULL)
		{
			//printk(KERN_INFO "Element: %s\n",c1);

			sscanf(c1,"%i:%x", &freq, &len_note);
			melody[melody_notes].freq = freq;
			melody[melody_notes].len = len_note;

			//printk(KERN_INFO "Nota: %i\n",melody[melody_notes].freq);
			//printk(KERN_INFO "Tiempo: %i\n",melody[melody_notes].len);

			c1 = strsep(&c2,",");

			melody_notes++;
		}

		played_notes = 0;

		schedule_work(&my_work);

		spin_unlock_irqrestore(&lock, flags);
	}
	else if(sscanf(c, "beat %i", &beat) == 1) {
		spin_lock_irqsave(&lock, flags);
		bpm = beat;
		spin_unlock_irqrestore(&lock, flags);
	}
	kfree(c);
	kfree(c2);

	return len;
}



/*
 * Called when a process closes the device file.
 */
static int buzzer_release(struct inode *inode, struct file *file){
	module_put(THIS_MODULE);
	return 0;
}

static void __exit pwm_module_exit(void)
{
	misc_deregister(&buzzer_misc);

		/* Release of the button */
	free_irq(gpio_button_irqn, NULL);

	/* Wait until defferred work has finished */
	flush_workqueue(my_workqueue);
	destroy_workqueue(my_workqueue);

	/* Wait until completion of the timer function (if it's currently running) and delete timer */
	del_timer_sync(&buzzer_timer);


	/* Release PWM device */
	pwm_disable(pwm_device);
	pwm_free(pwm_device);

	gpiod_put(desc_button);

	vfree(melody);

	printk(KERN_INFO "Module extracted Successfully\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* Work's handler function */
static void my_wq_function(struct work_struct *work)
{
	unsigned long int flags = 0;
	//printk(KERN_INFO "Accessing deferred work function\n");

	spin_lock_irqsave(&lock, flags);
	if(buzzer_request == REQUEST_CONFIG){
		buzzer_request = REQUEST_NONE;
		buzzer_state = BUZZER_STOPPED;
		spin_unlock_irqrestore(&lock, flags);
		return;
	}
	else if(buzzer_request == REQUEST_PAUSE)
	{
		buzzer_state = BUZZER_PAUSED;
		buzzer_request = REQUEST_NONE;
		pwm_disable(pwm_device);
		spin_unlock_irqrestore(&lock, flags);
		return;
	}
	else if(buzzer_request == REQUEST_START){
		buzzer_state = BUZZER_PLAYING;
	}
	else if(buzzer_request == REQUEST_RESUME)
		buzzer_state = BUZZER_PLAYING;

	buzzer_request = REQUEST_NONE;


	if(played_notes >= melody_notes){

		buzzer_state = BUZZER_STOPPED;
		pwm_disable(pwm_device);
		spin_unlock_irqrestore(&lock, flags);
		return;
	}

	next_note = &melody[played_notes];

	spin_unlock_irqrestore(&lock, flags);

	//printk(KERN_INFO "Note to play: %i,\nWith duration: %i\n",next_note->freq,next_note->len);

	pwm_init_state(pwm_device, &pwm_state);

	pwm_enable(pwm_device);

		/* Obtain period from frequency */
	pwm_state.period = freq_to_period_ns(next_note->freq);

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

		//MODTIMER que expira cuando la duración de la nota finaliza
	mod_timer(&buzzer_timer,jiffies + msecs_to_jiffies(calculate_delay_ms(next_note->len, bpm)));
}

static void timer_signal_function(struct timer_list *timer)
{
	unsigned long int flags = 0;

	//printk(KERN_INFO "Accessing timer signal function\n");

	//printk(KERN_INFO "state of buzzer is %i", buzzer_state);

	spin_lock_irqsave(&lock, flags);

	if(buzzer_state == BUZZER_STOPPED && buzzer_request== REQUEST_START){
		//printk(KERN_INFO "First entrance to timer Funct\n");
		schedule_work(&my_work);
	}
	else if(buzzer_state == BUZZER_PAUSED && buzzer_request== REQUEST_RESUME){
		schedule_work(&my_work);
	}
	else if(buzzer_state == BUZZER_PLAYING){
		played_notes++;
		schedule_work(&my_work);
	}
	spin_unlock_irqrestore(&lock, flags);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	unsigned long int flags = 0;

	#ifdef MANUAL_DEBOUNCE
	static unsigned long last_interrupt = 0;
	unsigned long diff = jiffies - last_interrupt;

	if(diff < 20)
		return IRQ_HANDLED;

	last_interrupt = jiffies;
	#endif

	spin_lock_irqsave(&lock, flags);

	if(buzzer_request == REQUEST_CONFIG){
		spin_unlock_irqrestore(&lock, flags);
		printk(KERN_INFO "Configuring melody, please try again later\n");
		return IRQ_HANDLED;
	}
	if(buzzer_state == BUZZER_STOPPED){
		played_notes = 0;
		buzzer_request = REQUEST_START;
	}
	else if(buzzer_state == BUZZER_PLAYING){
		buzzer_request = REQUEST_PAUSE;
	}
	else{	//BUZZER_PAUSED
		buzzer_request = REQUEST_RESUME;
	}
	spin_unlock_irqrestore(&lock, flags);

	mod_timer(&buzzer_timer,jiffies);

	//printk(KERN_INFO "Button has been pressed\n");

	return IRQ_HANDLED;
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

static void load_default_melody(void)
{
	int C4 = 26163,
		E4 =32963,
		G4 = 39200,
		C5 = 52325;

	struct music_step melodic_line[] = {
		{C4, 4}, {E4, 4}, {G4, 4}, {C5, 4}, {0, 2}, {C5, 4}, {G4, 4}, {E4, 4}, {C4, 4}, {0, 0} /* Terminator */
	};

	struct music_step *to_load;
	for (to_load = melodic_line; !is_end_marker(to_load); to_load++)
	{
		melody[melody_notes].freq = to_load->freq;
		melody[melody_notes].len = to_load->len;

		melody_notes++;
	}
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
