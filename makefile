FP_TYPE = float

CC = $(CROSS)gcc
CFLAGS = -DFP_TYPE=$(FP_TYPE) -Ofast -std=c99 -Wall -fPIC $(CFLAGSEXT) -g
OUT_DIR = ./build
OBJS = $(OUT_DIR)/deadfish.o $(OUT_DIR)/ciglet.o

default: deadfish

deadfish: $(OBJS)
	$(CC) $(CFLAGS) -o deadfish $(OBJS) -lm -Wno-unused-result

$(OUT_DIR)/deadfish.o: deadfish.c external/ciglet/ciglet.h

$(OUT_DIR)/ciglet.o: external/ciglet/ciglet.c external/ciglet/ciglet.h
	$(CC) $(CFLAGS) -o $(OUT_DIR)/ciglet.o -c external/ciglet/ciglet.c

$(OUT_DIR)/%.o : %.c
	$(CC) $(CFLAGS) -o $(OUT_DIR)/$*.o -c $*.c

clean:
	@echo 'Removing all temporary binaries... '
	@rm -f $(OUT_DIR)/deadfish.a $(OUT_DIR)/*.o
	@echo Done.
