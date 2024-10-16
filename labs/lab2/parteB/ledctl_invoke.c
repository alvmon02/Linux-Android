#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define __SYS_LED	451

long ledctl(unsigned int leds);


int main(int argc, char** argv)
{
	unsigned int leds = 0;

	if(argc != 2){
		printf("Accepts only one argument!\n");
		return EINVAL;
	}
	if(sscanf(argv[1], "0x%u", &leds))
		printf("Number not inserted in correct hexadecimal format: 0x0");

	if(ledctl(leds)){
		perror("Error en llamada sistema leds\n");
		return 1;
	}

	return 0;
}



long ledctl(unsigned int leds){
	return (long) syscall(__HALP,leds);
}
