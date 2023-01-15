# @author Andras Schloessl
# @date 14.01.2023

FLAGS = -std=c99 -pedantic -Wall -g -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L

.PHONY: all clean
all: dependencies client server

dependencies: http

http:
	gcc $(FLAGS) -o $@.o -c $@.c

client:
	gcc $(FLAGS) -o $@.o -c $@.c
	gcc $(FLAGS) -o $@ http.o $@.o

server:
	gcc $(FLAGS) -o $@.o -c $@.c
	gcc $(FLAGS) -o $@ http.o $@.o

clean:
	rm -f *.o client server
