XC_PRIVATE := ../libxc/xc_private.h

CC      := gcc
CCFLAGS := -Wall -Wextra -g
LDFLAGS := -lxenctrl

LN := ln -s
RM := rm


default: all

all: trigger


trigger: trigger.c xc_private.h
	$(CC) $(CCFLAGS) $< -o $@ $(LDFLAGS) -I./

xc_private.h:
	$(LN) $(XC_PRIVATE) $@


clean:
	-$(RM) trigger xc_private.h *~
