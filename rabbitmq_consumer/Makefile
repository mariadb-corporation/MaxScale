include buildconfig.inc

CC=gcc
LIBFLAGS=$(addprefix -L,$(LIBRARY_DIRS))
INCFLAGS=$(addprefix -I,$(INCLUDE_DIRS))
CFLAGS=-c -Wall -g -Iinih $(INCFLAGS)
LDFLAGS= $(LIBFLAGS) -lrabbitmq -lmysqlclient
SRCS= inih/ini.c consumer.c
OBJ=$(SRCS:.c=.o)
all:$(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o consumer
%.o:%.c
	$(CC) $(CFLAGS)  $< -o $@

clean:
	-rm *.o
	-rm *~
