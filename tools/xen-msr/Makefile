default: all

all: xen-msr


xen-msr: xen-msr.o hypercall.o execute.o parsing.o
	gcc $^ -o $@ -lpthread -lrt -lxenctrl

%.o: %.c xen-msr.h
	gcc -Wall -Wextra -pedantic -O2 -c $< -o $@

hypercall.o: hypercall.c xen-msr.h
	gcc -Wall -Wextra -O2 -I./ -c $< -o $@

clean:
	-rm *.o *~ xen-msr
