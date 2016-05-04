TARGET = kernel2minor
CFLAGS = -O2 -Wall -DCONFIG_YAFFS_UTIL -DCONFIG_YAFFS_DEFINES_TYPES
KERNEL2MINOR_SRCS = kernel2minor.c
KERNEL2MINOR_OBJS = $(patsubst %,%,$(KERNEL2MINOR_SRCS:.c=.o))
YAFFS2_DIR = ./yaffs2
YAFFS2_SRCS = yaffs_ecc.c yaffs_packedtags2.c yaffs_hweight.c
YAFFS2_OBJS = $(patsubst %,$(YAFFS2_DIR)/%,$(YAFFS2_SRCS:.c=.o))

## Change if you are using a cross-compiler
#MAKETOOLS = mips-openwrt-linux-

CC=$(MAKETOOLS)gcc

all: $(TARGET)
$(TARGET): $(KERNEL2MINOR_OBJS) $(YAFFS2_OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(KERNEL2MINOR_OBJS) $(YAFFS2_OBJS)


#lets build our kernel2minor files(currently only one file)
$(KERNEL2MINOR_SRCS):
$(KERNEL2MINOR_OBJS): %.o: %.c
	 $(CC) $(CFLAGS) -c $< -o $@

#lets build alien files from yaffs2 project
$(YAFFS2_SRCS):
$(YAFFS2_OBJS): %.o: %.c
	 $(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.bin $(TARGET) $(YAFFS2_DIR)/*.o
