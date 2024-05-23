CC?=cc
CFLAGS?=-O2
PREFIX?=/usr/local

pocsag : pocsag.c
	$(CC) -o pocsag $(CFLAGS) --std c99 -Wall -o pocsag pocsag.c

.PHONY: clean install
clean :
	rm pocsag

install : pocsag
	install --mode 755 -D -t $(DESTDIR)$(PREFIX)/bin pocsag
