all: pocsag

pocsag: pocsag.c
	gcc -O2 --std c99 -o pocsag pocsag.c

clean:
	rm pocsag
