CC = gcc
CFLAGS = -Wall -Werror
LDLIBS = -lrt -lm -lxenctrl

NB_NODES = 8
CFLAGS += -DNB_NODES=${NB_NODES}

.PHONY: all clean

all: xen-carrefour

xen-carrefour: xen-carrefour.c hypercall.c perfctr.c xencarrefour.c carrefour.h xc_private.h xencarrefour.h

clean:
	rm -f xen-carrefour *.o
