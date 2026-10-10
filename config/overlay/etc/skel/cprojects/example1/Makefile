CC = tcc
CFLAGS = $(shell pkg-config --cflags sdl3 fluidsynth)
LDLIBS = $(shell pkg-config --libs sdl3 fluidsynth)

example1: main.c music.c music.h
	$(CC) $(CFLAGS) -o $@ main.c music.c $(LDLIBS)

run: example1
	./example1

win32: CROSS = i686-w64-mingw32
win64: CROSS = x86_64-w64-mingw32
win32 win64:
	$(CROSS)-gcc -O2 -o example1-$@.exe main.c music.c $$($(CROSS)-pkg-config --cflags --libs sdl3 fluidsynth)

clean:
	rm -f example1 example1-win32.exe example1-win64.exe

.PHONY: run win32 win64 clean
