CC = tcc
CFLAGS = $(shell pkg-config --cflags raylib)
LDLIBS = $(shell pkg-config --libs raylib)

raylib3d: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

run: raylib3d
	./raylib3d

win32: CROSS = i686-w64-mingw32
win64: CROSS = x86_64-w64-mingw32
win32 win64:
	$(CROSS)-gcc -O2 -o raylib3d-$@.exe main.c $$($(CROSS)-pkg-config --cflags --libs raylib)

clean:
	rm -f raylib3d raylib3d-win32.exe raylib3d-win64.exe

.PHONY: run win32 win64 clean
