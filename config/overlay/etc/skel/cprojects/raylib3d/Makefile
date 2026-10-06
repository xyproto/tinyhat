CC = tcc
CFLAGS = $(shell pkg-config --cflags raylib)
LDLIBS = $(shell pkg-config --libs raylib)

raylib3d: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

run: raylib3d
	./raylib3d

clean:
	rm -f raylib3d

.PHONY: run clean
