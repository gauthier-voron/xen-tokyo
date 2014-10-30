default: all

all: xen-trigger


xen-trigger: xen-trigger.c xc_private.h
	gcc -Wall -Wextra -O2 -g $< -o $@ -I./ -lxenctrl


clean:
	rm -rf *~ xen-trigger
