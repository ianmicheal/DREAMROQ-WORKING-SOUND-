

KOS_CFLAGS+= -std=c99 -O3  -ffast-math -ffp-contract=fast -fomit-frame-pointer -flto  -mpretend-cmove -falign-loops=4 -falign-labels=2 -falign-functions=32 -falign-jumps=2 -Wno-unused -Wno-format -fno-exceptions -DINLINE=static__inline__   -D__SH4_SINGLE_ONLY__ -D__LITTLE_ENDIAN__
# Put the filename of the output binary here
TARGET = dreamroq.elf

# List all of your C files here, but change the extension to ".o"
OBJS = dreamroq-player.o dreamroqlib.o
OBJS += libdcmc/timer.o

#AICA Audio Driver
KOS_CFLAGS += -I. -Ilibdcmc/
OBJS += libdcmc/snd_stream.o
OBJS += libdcmc/snddrv.o

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean:
	-rm -f $(TARGET) $(OBJS)

rm-elf:
	-rm -f $(TARGET)

# If you don't need a ROMDISK, then remove "romdisk.o" from the next few
# lines. Also change the -l arguments to include everything you need,
# such as -lmp3, etc.. these will need to go _before_ $(KOS_LIBS)
$(TARGET): $(OBJS) 
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $(TARGET) $(KOS_START) \
		$(OBJS) $(OBJEXTRA) $(KOS_LIBS) 

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist:
	rm -f $(OBJS) romdisk.o romdisk.img
	$(KOS_STRIP) $(TARGET)

