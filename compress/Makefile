#
# Standalone Makefile for ZX297520V3 U-Boot Self-Extracting Wrapper (LZMA / GZIP)
#

CROSS_COMPILE ?= arm-linux-gnueabi-
CC            := $(CROSS_COMPILE)gcc
AR            := $(CROSS_COMPILE)ar
OBJCOPY       := $(CROSS_COMPILE)objcopy

IMAGE_START   ?= 0x21DF0000
INPUT_BIN     ?= ../u-boot.bin
COMPRESS_TYPE ?= lzma

CFLAGS        := -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -fpic \
                 -fno-stack-protector -fno-builtin -marm -march=armv7-a
AFLAGS        := -fpic -marm -march=armv7-a
LDFLAGS       := -Wl,--no-check-sections -nostartfiles -Tcompressed.ld \
                 -nostdlib -Wl,-static

ifeq ($(COMPRESS_TYPE),lzma)
CFLAGS        += -DUSE_LZMA
AFLAGS        += -DUSE_LZMA
COBJS         := decompress.o LzmaDec.o misc.o
LIB           := liblzma.a
PAYLOAD       := compressed.lzma
else
COBJS         := decompress.o decompress_inflate.o inffast.o inflate.o inftrees.o misc.o
LIB           := libgzip.a
PAYLOAD       := compressed.gz
endif

all: compressed.bin

compressed.gz: $(INPUT_BIN)
	@echo "  GZIP    $(INPUT_BIN) -> $@"
	gzip -9 -n -c $(INPUT_BIN) > $@

compressed.lzma: $(INPUT_BIN)
	@echo "  LZMA    $(INPUT_BIN) -> $@"
	xz --format=lzma --lzma1=preset=9,lc=3,lp=0,pb=2 -c $(INPUT_BIN) > $@

head.o: head.S
	@echo "  AS      $<"
	$(CC) $(AFLAGS) -c $< -o $@

compressed.o: compressed.S $(PAYLOAD)
	@echo "  AS      $< ($(PAYLOAD))"
	$(CC) $(AFLAGS) -DIMAGE_START=$(IMAGE_START) -Wa,-I. -c $< -o $@

$(COBJS): %.o: %.c
	@echo "  CC      $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(COBJS)
	@echo "  AR      $@"
	$(AR) rcs $@ $(COBJS)

compressed.elf: head.o compressed.o $(LIB) compressed.ld
	@echo "  LD      $@"
	$(CC) $(AFLAGS) $(LDFLAGS) -Wl,-Map,compressed.map -o $@ head.o compressed.o $(LIB)

compressed.bin: compressed.elf
	@echo "  OBJCOPY $@"
	$(OBJCOPY) -O binary $< $@
	@echo "Compressed bootloader binary size: `wc -c < $@` bytes"

clean:
	rm -f *.o *.a *.elf *.bin *.map compressed.gz compressed.lzma

.PHONY: all clean
