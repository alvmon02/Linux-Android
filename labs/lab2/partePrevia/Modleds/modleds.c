#include <linux/module.h> 
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>

#define ALL_LEDS_ON 0x7


#define SOME_LED3 0b100		//Ésta es la luz de Mayusc
#define SOME_LED2 0b010		//Ésta es la luz de NUM
#define SOME_LED1 0b001		//Ésta es la luz de BLOCs
#define ALL_LEDS_OFF 0


struct tty_driver* kbd_driver= NULL;
struct tty_driver* get_kbd_driver_handler(void);

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
	printk(KERN_INFO "modleds: loading\n");
	printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
	return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}

static int __init modleds_init(void)
{	
	kbd_driver= get_kbd_driver_handler();
	set_leds(kbd_driver,SOME_LED2 |SOME_LED1);
	//set_leds(kbd_driver,SOME_LED2);
	//set_leds(kbd_driver,SOME_LED3);
	//set_leds(kbd_driver,SOME_LED4);
	//set_leds(kbd_driver,SOME_LED5);
	//set_leds(kbd_driver,SOME_LED6);
	return 0;
}

static void __exit modleds_exit(void){
	set_leds(kbd_driver,ALL_LEDS_OFF); 
}

module_init(modleds_init);
module_exit(modleds_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
