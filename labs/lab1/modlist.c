#include <linux/module.h>       /* Requerido por todos los módulos */
#include <linux/kernel.h>       /* Definición de KERN_INFO */
MODULE_LICENSE("GPL");  /*  Licencia del modulo */

	//Preprocesadores
int modulo_Practica1_init(void);
void modulo_Practica1_clean(void);


	/* Nodo fantasma (cabecera) de la lista enlazada */
struct list_head mylist; 
	/* Estructura que representa los nodos de la lista */
struct list_item {
	int data;
	struct list_head links;
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_Practica1_init(void)
{
	printk(KERN_INFO "Modulo LIN cargado. Hola kernel.\n");

		/* Devolver 0 para indicar una carga correcta del módulo */
	return 0;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_Practica1_clean(void)
{
        printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}

/* Declaración de funciones init y exit */
module_init(modulo_Practica_init);
module_exit(modulo_Practica1_clean);
