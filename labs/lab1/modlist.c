#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");  /*  Licencia del modulo */

	//Preprocesadores, para escribir las funciones sin importar el órden
int modulo_Practica1_init(void);
void modulo_Practica1_clean(void);
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);


	//Definición de estructuras necesarias
static const struct proc_ops proc_entry_fops = {
	.proc_read = modlist_read,
	.proc_write = modlist_write,    
};

		// Nodo fantasma (cabecera) de la lista enlazada
struct list_head mylist; 
		//Estructura que representa los nodos de la lista
struct list_item {
	int data;
	struct list_head links;
};


	//Variables Globales
static struct proc_dir_entry *proc_entry;

	//TODO faltan las declaraciones de memoria con kmalloc, sólo está la creación del archivo modlist en /proc  
/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_Practica1_init(void)
{

	proc_entry = proc_create("modlist", 0666, NULL, &proc_entry_fops);

	if(proc_entry == NULL){
		kfree("modlist");
		printk(KERN_INFO,"Modlist: No se pudo crear /proc/modlist entry\n");
		return -ENOMEM;
	}
	else
		printk("Módulo modlist cargado con éxito\n"); 

	return 0;
}

	//TODO
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	return 0;
}

	//TODO
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off){
	return 0;
}



/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_Practica1_clean(void)
{
	remove_proc_entry("modlist", NULL);
	kfree(modlist);
	printk(KERN_INFO,"Modlist: Módulo extraído con éxito\n");
}

/* Declaración de funciones init y exit */
module_init(modulo_Practica1_init);
module_exit(modulo_Practica1_clean);
