CC = gcc
CFLAGS = -Wall -O0 -g -pthread
#LDFLAGS = -Wall -O0 -g -pthread

all: ledctl_invoke

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

ledctl_invoke: ledctl_invoke.o
	$(CC) $(LDFLAGS) -o $@ $<

.PHONY: clean all


clean:
	rm ledctl_invoke.o ledctl_invoke
