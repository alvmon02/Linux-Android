#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define __HALP	451

long ledctl(unsigned int leds){
	return (long) syscall(__HALP,leds);
}

int main(int argc, char** argv)
{
	unsigned int leds = 0;


	if(argc < 2)
		return -EINVAL;

	if(argc > 2)
		return -E2BIG;

	sscanf(argv[1], "0x%u", &leds);

	if(ledctl(leds)){
		perror("Error en llamada sistema leds\n");
		return -1;
	}

	return 0;
}
