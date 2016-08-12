all: pocsag

pocsag: pocsag.c
	gcc -O2 -o pocsag pocsag.c
