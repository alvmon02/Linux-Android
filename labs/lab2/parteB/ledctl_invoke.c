#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __SYS_LED	451

long ledctl(unsigned int leds);


int main(int argc, char** argv)
{
	unsigned int leds = 0;
	
	if(argc != 2){
		printf("Accepts only one argument!\n");
		return EXIT_FAILURE;
	}
	
	if((sscanf(argv[1], "0x%u", &leds)) != 1){
		printf("Number not inserted in correct hexadecimal format: 0x(Number in Hexadecimal)\n");
		return EXIT_FAILURE;
	}
	if(ledctl(leds)){
		perror("Error en llamada sistema leds\n");
		return EXIT_FAILURE;
	}

	return 0;
}



long ledctl(unsigned int leds){
	return (long) syscall(__SYS_LED,leds);
}
