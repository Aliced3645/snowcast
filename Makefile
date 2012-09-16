CC = gcc
DEBUGFLAGS = -g -Wall
CFLAGS = -D_REENTRANT $(DEBUGFLAGS) -D_XOPEN_SOURCE=500 -lpthread
LDFLAGS = -lpthread 

all: snowcast_listener snowcast_control snowcast_server test

snowcast_listener: snowcast_listener.c
snowcast_control:  snowcast_control.c 
snowcast_server:   snowcast_server.c
test:	test.c
clean:
	rm -f snowcast_listener snowcast_control snowcast_server
