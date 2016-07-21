# Compilation Verbosity

V ?= 1

ifeq ($(V),1)
  define cmd-print
    @echo '$1'
  endef
endif

ifneq ($(V),2)
  GNUMAKEFLAGS += --no-print-directory
  Q := @
endif


# V ?= 1

# ifneq ($(V),3)
#   Q := @
# else
#   MAKEFLAGS += --no-print-directory
# endif

# ifeq ($(V),2)
#   define cmd-info
#     @echo '$(1)'
#   endef
# endif

# ifeq ($(V),1)
#   define cmd-print
#     @echo '$(1)'
#   endef
# endif


# define cmd-make
#   $(call cmd-info, MAKE    $(strip $(1)))
#   $(Q)+$(MAKE) $(3) -C $(1) $(2)
# endef

# define cmd-cc
#   $(call cmd-print,  CC      $(1))
#   $(Q)$(CC) $(3) -c $(2) -o $(1)
# endef

# define cmd-cpp
#   $(call cmd-print,  CPP     $(1))
#   $(Q)$(CPP) $(3) $(2) -o $(1)
# endef
