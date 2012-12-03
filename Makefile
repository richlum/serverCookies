CC=gcc
CFLAGS=-Wall -Werror -g -Wextra -Wno-unused-parameter 
LDFLAGS=

all: cshttp
cshttp: cshttp.o service.o util.o mytime.o
test_util: test_util.o util.o

cshttp.o: cshttp.c service.h
service.o: service.c service.h util.h
util.o: util.c util.h
test_util.o: test_util.c util.h
mytime.o: mytime.c mytime.h


clean:
	-rm -rf cshttp.o service.o util.o cshttp test_util.o mytime.o

#############
debug: CFLAGS += -DDEBUG 
debug: all
