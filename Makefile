CC    = gcc
FLAGS = -O2 -Wall -Wextra -ggdb3 

UVash: UVash.c
	$(CC) -o UVash $(FLAGS) UVash.c

all:
	UVash

clean:
	rm -f UVash
