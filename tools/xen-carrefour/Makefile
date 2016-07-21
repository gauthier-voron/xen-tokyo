CC = gcc
CFLAGS = -Wall -Wextra -O2
LDLIBS = -lrt -lm -lpthread -lxenctrl

NB_NODES = 8
CFLAGS += -DNB_NODES=${NB_NODES}


all: xen-carrefour

clean:
	-rm -f xen-carrefour *.o

install:
	test -e xen-carrefour
	mkdir -p $(DESTDIR)/usr/bin
	cp xen-carrefour $(DESTDIR)/usr/bin/xen-carrefour

uninstall:
	rm $(DESTDIR)/usr/bin/xen-carrefour
	rmdir $(DESTDIR)/usr/bin 2>/dev/null || true
	rmdir $(DESTDIR)/usr 2>/dev/null || true


xen-carrefour: xen-carrefour.c hypercall.c perfctr.c xencarrefour.c \
               carrefour.h xc_private.h xencarrefour.h model.c model.h

.PHONY: all clean
