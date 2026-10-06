CC = tcc
CFLAGS = $(shell pkg-config --cflags sdl3 fluidsynth)
LDLIBS = $(shell pkg-config --libs sdl3 fluidsynth)

example1: main.c music.c music.h
	$(CC) $(CFLAGS) -o $@ main.c music.c $(LDLIBS)

run: example1
	./example1

clean:
	rm -f example1

.PHONY: run clean
