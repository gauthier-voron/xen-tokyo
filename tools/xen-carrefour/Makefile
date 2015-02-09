LIBGSL = $(shell gsl-config --libs)

CC = gcc
CFLAGS = -Wall -Werror
LDLIBS = -lpthread -lnuma ${LIBGSL}

NB_NODES = $(shell lscpu | awk '/NUMA node\(s\)/ {print $$3}')
CFLAGS += -DNB_NODES=${NB_NODES}

.PHONY: all clean

all: carrefour-kthp tags

carrefour-kthp: carrefour-kthp.c carrefour.h

tags: carrefour-kthp.c carrefour.h
	ctags --totals `find . -name '*.[ch]'`
	cscope -b -q -k -R -s.

clean_tags:
	rm cscope.*
	rm tags

clean: clean_tags
	rm -f carrefour-kthp *.o
