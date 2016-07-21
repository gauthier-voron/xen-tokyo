default: all


all: xen-trigger

clean:
	-rm -rf *~ xen-trigger

install:
	test -e xen-trigger
	mkdir -p $(DESTDIR)/usr/bin
	cp xen-trigger $(DESTDIR)/usr/bin/xen-trigger

uninstall:
	rm $(DESTDIR)/usr/bin/xen-trigger
	rmdir $(DESTDIR)/usr/bin 2>/dev/null || true
	rmdir $(DESTDIR)/usr 2>/dev/null || true


xen-trigger: xen-trigger.c xc_private.h
	gcc -Wall -Wextra -O2 -g $< -o $@ -I./ -lxenctrl


