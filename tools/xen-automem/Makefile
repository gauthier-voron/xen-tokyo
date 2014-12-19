default: all

all: xen-automem


xen-automem: xen-automem.c xc_private.h
	gcc -Wall -Wextra -O2 -g $< -o $@ -I./ -lxenctrl -lrt


clean:
	rm -rf *~ xen-automem
