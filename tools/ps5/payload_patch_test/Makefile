ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS := -std=c11 -O2 -Wall -Wextra
LDLIBS := -lkernel -lSceSystemService -lSceUserService -lpthread

all: PSBox_Trophies.elf

PSBox_Trophies.elf: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

clean:
	rm -f PSBox_Trophies.elf patch_test.elf
