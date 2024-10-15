#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>

#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>

#define ON_LEDS  0x7

#define CAPS_LED 0b100     //Ésta es la verdadera luz de Mayusc
#define NUM_LED  0b010     //Ésta es la verdadera luz de NUM
#define BLOC_LED 0b001     //Ésta es la verdadera luz de BLOCs


#define ICAPS_LED 0b010     //Ésta es la implementación luz de Mayusc
#define INUM_LED  0b100     //Ésta es la implementación luz de NUM
#define IBLOC_LED 0b001     //Ésta es la implementación luz de BLOCs

#define OFF_LEDS 0


struct tty_driver* kbd_driver= NULL;
struct tty_driver* get_kbd_driver_handler(void);
static inline int set_leds(struct tty_driver* handler, unsigned int mask);

SYSCALL_DEFINE1(ledctrl, unsigned int, leds)
{
	unsigned int real_Leds = 0;
	
	if(leds < OFF_LEDS || leds > ON_LEDS)
		return -EINVAL;

	kbd_driver= get_kbd_driver_handler();

	if((leds & ICAPS_LED) == ICAPS_LED)
		real_Leds |= CAPS_LED;
	
	if((leds & INUM_LED) == INUM_LED)
		real_Leds |= NUM_LED;
	
	if((leds & IBLOC_LED) == IBLOC_LED)
		real_Leds |= BLOC_LED;

	set_leds(kbd_driver,real_Leds);
	
	return 0;
}


	//Get driver handler
struct tty_driver* get_kbd_driver_handler(void){
    printk(KERN_INFO "modleds: loading\n");
    printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}


//////////////////////////////////////////////////
#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>

#define ON_LEDS  0x7

#define CAPS_LED 0b100     //Ésta es la verdadera luz de Mayusc
#define NUM_LED  0b010     //Ésta es la verdadera luz de NUM
#define BLOC_LED 0b001     //Ésta es la verdadera luz de BLOCs


#define ICAPS_LED 0b010     //Ésta es la implementación luz de Mayusc
#define INUM_LED  0b100     //Ésta es la implementación luz de NUM
#define IBLOC_LED 0b001     //Ésta es la implementación luz de BLOCs

#define OFF_LEDS 0


struct tty_driver* get_kbd_driver_handler(void);
static inline int set_leds(struct tty_driver* handler, unsigned int mask);

	//Get driver handler
struct tty_driver* get_kbd_driver_handler(void){
    printk(KERN_INFO "modleds: loading\n");
    printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    printk(KERN_INFO "modleds: into set_leds function\n");

    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}


SYSCALL_DEFINE1(ledctl, unsigned int, leds)
{
	unsigned int real_Leds = 0;
	struct tty_driver* kbd_driver= NULL;
	
	if(leds < OFF_LEDS || leds > ON_LEDS)
		return -EINVAL;
	printk(KERN_INFO"leds in: %u\n",leds);

	kbd_driver= get_kbd_driver_handler();

	if((leds & ICAPS_LED) == ICAPS_LED)
		real_Leds |= CAPS_LED;
	
	if((leds & INUM_LED) == INUM_LED)
		real_Leds |= NUM_LED;
	
	if((leds & IBLOC_LED) == IBLOC_LED)
		real_Leds |= BLOC_LED;

	printk(KERN_INFO"leds real: %u\n",real_Leds);


	set_leds(kbd_driver,real_Leds);
	
	return 0;
}
