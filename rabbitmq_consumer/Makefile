include buildconfig.inc

CC=gcc
CFLAGS=-c -Wall -g -Iinih $(INCLUDE_DIRS)
LDFLAGS= $(LIBRARY_DIRS) -lrabbitmq -lmysqlclient
SRCS= inih/ini.c consumer.c
OBJ=$(SRCS:.c=.o)
all:$(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o consumer
%.o:%.c
	$(CC) $(CFLAGS)  $< -o $@

clean:
	-rm *.o
	-rm *~
