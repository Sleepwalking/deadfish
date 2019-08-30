PREFIX = /usr
CC = $(CROSS)gcc

CONFIG = Debug

CIGLET_PREFIX = /usr
CIGLET_A = $(CIGLET_PREFIX)/lib/libciglet.a
CIGLET_INCLUDE = $(CIGLET_PREFIX)/include/

ifeq 'Darwin' '$(shell uname)'
  CFLAGS_PLAT =
else
  CFLAGS_PLAT = -fopenmp
endif

OUT_DIR = ./build
OBJS = $(OUT_DIR)/deadfish.o

CFLAGS_COMMON = -DFP_TYPE=float -std=c99 -Wall -Wno-unused-result -fPIC \
  $(CFLAGS_PLAT) -I$(CIGLET_INCLUDE)
CFLAGS_DBG = $(CFLAGS_COMMON) -Og -g
CFLAGS_REL = $(CFLAGS_COMMON) -Ofast
ifeq ($(CONFIG), Debug)
  CFLAGS = $(CFLAGS_DBG)
else
  CFLAGS = $(CFLAGS_REL)
endif

default: deadfish

deadfish: $(OBJS)
	$(CC) $(CFLAGS) -o deadfish $(OBJS) $(CIGLET_A) -lm

$(OUT_DIR)/deadfish.o: deadfish.c

$(OUT_DIR)/%.o : %.c
	mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) -o $(OUT_DIR)/$*.o -c $*.c

install: deadfish
	mkdir -p $(PREFIX)/bin
	cp deadfish $(PREFIX)/bin

clean:
	@echo 'Removing all temporary binaries... '
	@rm -f deadfish $(OUT_DIR)/*.o
	@echo Done.
