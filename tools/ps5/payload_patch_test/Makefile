ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS := -std=c11 -O2 -g -Wall -Wextra
LDLIBS := -lkernel -lSceSystemService -lSceUserService -lpthread

all: patch_test.elf

patch_test.elf: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

clean:
	rm -f patch_test.elf
