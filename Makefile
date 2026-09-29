CC = gcc
CFLAGS = -Wall -O2
LIBS = -lX11 -lXcomposite -lXdamage -lXrender -lXfixes -lXext -lm
PREFIX = /usr/local

all: vzam

vzam: vzam.c config.h
	$(CC) $(CFLAGS) vzam.c -o vzam $(LIBS)

install: vzam
	install -Dm755 vzam $(DESTDIR)$(PREFIX)/bin/vzam

clean:
	rm -f vzam

.PHONY: all install clean
