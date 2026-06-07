CROSS_COMPILE	?= 

CC		:= $(CROSS_COMPILE)gcc
CFLAGS		:= -W -Wall -g -O3
LDFLAGS		:= -g

all: uvc-gadget

uvc-gadget: uvc-gadget.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgphoto2 -lpthread

clean:
	rm -f *.o
	rm -f uvc-gadget