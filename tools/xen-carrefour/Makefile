CC = gcc
CFLAGS = -Wall -Werror
LDLIBS = -lrt -lm -lxenctrl

NB_NODES = 8
CFLAGS += -DNB_NODES=${NB_NODES}

.PHONY: all clean

all: carrefour-kthp tags

carrefour-kthp: carrefour-kthp.c hypercall.c perfctr.c xencarrefour.c carrefour.h xc_private.h xencarrefour.h

tags: carrefour-kthp.c carrefour.h
	ctags --totals `find . -name '*.[ch]'`

clean_tags:
	rm tags

clean: clean_tags
	rm -f carrefour-kthp *.o
