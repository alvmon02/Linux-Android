#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>

#define MAX_LEN 128


MODULE_LICENSE("GPL");  /*  Licencia del modulo */

//Preprocesadores, para escribir las funciones sin importar el órden
int modulo_Practica1_init(void);
void modulo_Practica1_clean(void);
static ssize_t modlist_read(struct file* filp, char __user* buf, size_t len, loff_t* off);
static ssize_t modlist_write(struct file* filp, const char __user* buf, size_t len, loff_t* off);
void print_list_dmesg(struct list_head* list);

//Definición de estructuras necesarias
static const struct proc_ops proc_entry_fops = {
	.proc_read = modlist_read,
	.proc_write = modlist_write,
};

//Estructura que representa los nodos de la lista
struct list_item {
	int data;
	struct list_head links;
};

#define ITEM_SIZE sizeof(struct list_item)


//Variables Globales
static struct proc_dir_entry* proc_entry;

// Nodo fantasma (cabecera) de la lista enlazada
LIST_HEAD(myList);
/**
 * OJO, éstas dos declaraciones son casi iguales,
 * de hecho, la macro añade el INIT_LIST_HEAD,
 * es ambos la declaración del nodo fantasma y su inicialización,
 * motivo por el que elimino la declaración de myList como struct
 * struct list_head myList; == LIST_HEAD(myList);
 */


	/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_Practica1_init(void)
{

	proc_entry = proc_create("modlist", 0666, NULL, &proc_entry_fops);

	if (proc_entry == NULL) {
		printk(KERN_INFO "Modlist: No se pudo crear /proc/modlist entry\n");
		return -ENOMEM;
	}
	else
		printk("Módulo modlist cargado con éxito\n");

	return 0;
}


static ssize_t modlist_read(struct file* filp, char __user* buf, size_t len, loff_t* off)
{
	int bytesRead = 0;
	char kbuf[MAX_LEN] = "",
		 aux[10] = "";
	char *dest = kbuf;

	struct list_item* item = NULL;
	struct list_head* cur_node = NULL;


	 if ((*off) > 0) /* Tell the application that there is nothing left to read */
	 	return bytesRead;


		//Para depurar, saber si se añaden los elementos correctamente, se comprueban en dmesg
	//print_list_dmesg(&myList);
	
		//Extracción de elementos para escritura a panalla
	list_for_each(cur_node, &myList) {
	
		item = list_entry(cur_node, struct list_item, links);
		bytesRead += sprintf(aux,"%d\n",item->data);

		if(bytesRead > MAX_LEN)
			return -ENOMEM;
		
		dest += sprintf(dest,"%d\n",item->data);
	}


	bytesRead = dest - kbuf;
	
	if(copy_to_user(buf, kbuf, bytesRead))
		return -EFAULT;

		/**Actualización de puntero de archivo para 
		 *limitar la lectura a una sóla iteración */
	(*off)+=bytesRead; 
	
	return bytesRead;
}


static ssize_t modlist_write(struct file* filp, const char __user* buf, size_t len, loff_t* off)
{

	// primero procesar remove / cleanup / add según el caso

	int value = 0;

	char c[MAX_LEN + 1];

	struct list_item* tempList = NULL;

	if (len > MAX_LEN)
		return -1;

	if (copy_from_user(c, buf, len))
		return -EFAULT;

	c[len] = '\0';



	// add creo que se procesa así
	if (sscanf(c, "add %i", &value)) {
		printk(KERN_INFO "Entrando en la parte ADD\n");

		tempList = kmalloc(ITEM_SIZE, GFP_KERNEL);

		tempList->data = value;

		INIT_LIST_HEAD(&tempList->links);

		list_add_tail(&tempList->links, &myList);
		printk(KERN_INFO "Parte ADD completada con éxito\n");
	}
	// remove borra todas las apariciones del elemento de la lista
	else if (sscanf(c, "remove %d", &value)) {

		printk(KERN_INFO "Entrando en la sección REMOVE\n");

		// Recorre la lista eliminando todas las apariciones del valor value
		// y liberando la memoria 
		struct list_item* item = NULL;
		struct list_item* actual, * aux;
		list_for_each_entry_safe(actual, aux, &myList, links) {
			item = list_entry(&actual->links, struct list_item, links);
			if (item->data == value) {
				list_del(&actual->links);
				kfree(actual);
			}
		}
		
		printk(KERN_INFO "Parte REMOVE completada con éxito\n");
	}
	// cleanup borra todos los elementos de la lista
	else if (strcmp(c, "cleanup\n") == 0) {

		printk(KERN_INFO "Entrando a la parte CLEANUP\n");

		/* Recorre la lista y libera la memoria */
		struct list_item* actual, * aux;
		list_for_each_entry_safe(actual, aux, &myList, links) {
			list_del(&actual->links);
			kfree(actual);
		}
		
		printk(KERN_INFO "Parte CLEANUP completada con éxito\n");
	}
	else
		return -EINVAL;

	(*off)+=len; 
	
	return len;
}

void print_list_dmesg(struct list_head* list)
{
	struct list_item* item = NULL;
	struct list_head* cur_node = NULL;

	/*Traversing Linked List and Print its Members*/
	// list_for_each_entry(item, &Head_Node, list) {
	// 	printk(KERN_INFO "Node %d data = %d\n", count++, temp->data);
	// }

	list_for_each(cur_node, list) {
		/* item points to the structure wherein the links are embedded */
		item = list_entry(cur_node, struct list_item, links);
		printk(KERN_INFO "Elemento de la lista: %i\n", item->data);
	}
}


/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_Practica1_clean(void)
{
	struct list_item *item=NULL;
	struct list_head *cur_node=NULL,
					 *aux = NULL;

	remove_proc_entry("modlist", NULL);


	list_for_each_safe(cur_node, aux, &myList) {
			//Elemento a eliminar
		item = list_entry(cur_node, struct list_item, links);
		
			//Eliminación del nodo respecto de la lista
		list_del(cur_node);
		
			//Eliminación de la información del nodo de forma dinámica
		kfree(item);
	}
	
	printk(KERN_INFO "Módulo extraído con éxito\n");
}

/* Declaración de funciones init y exit */
module_init(modulo_Practica1_init);
module_exit(modulo_Practica1_clean);
