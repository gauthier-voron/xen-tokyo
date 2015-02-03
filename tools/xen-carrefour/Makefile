CFLAGS = -Wall
LDLIBS = -lpthread -lrt -lxenctrl -lm

.PHONY: all clean

all: carrefour

carrefour: carrefour.c carrefour.h xencarrefour.c xencarrefour.h \
           perfctr.c hypercall.c xc_private.h

tags: carrefour.c carrefour.h xencarrefour.c xencarrefour.h
	ctags --totals `find . -name '*.[ch]'`
	cscope -b -q -k -R -s.

clean_tags:
	-rm cscope.*
	-rm tags

clean: clean_tags
	-rm -f carrefour *o
