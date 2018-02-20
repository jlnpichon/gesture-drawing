CC=gcc

CFLAGS= $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS= $(shell pkg-config --libs gtk+-3.0) -lm

all:
	$(CC) $(CFLAGS) -O0 -g -c utils.c -o utils.o
	$(CC) $(CFLAGS) -O0 -g main.c -o slideshow utils.o $(LDFLAGS)

clean:
	@rm *.o slideshow
