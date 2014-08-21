ifndef LIBPATH
LIBPATH=/usr/lib64
endif
ifndef INCPATH
INCPATH=/usr/include
endif
CC=gcc
CFLAGS=-c -Wall -g -Iinih -I$(INCPATH) -I$(INCPATH)/mysql
LDFLAGS= -L$(LIBPATH) -lrabbitmq -lmysqlclient
SRCS= inih/ini.c consumer.c
OBJ=$(SRCS:.c=.o)
all:$(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o consumer
%.o:%.c
	$(CC) $(CFLAGS)  $< -o $@

clean:
	-rm *.o
	-rm *~
