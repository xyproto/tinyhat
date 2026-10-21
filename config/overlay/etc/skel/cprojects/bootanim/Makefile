CC = tcc
CFLAGS = -Wall
DEST = /var/lib/tinyhat/bootanim/hat

install: hat preview
	./preview --test
	doas install -Dm755 hat $(DEST)
	@if mountpoint -q /var/lib/tinyhat; then echo "The new boot animation is used from the next boot"; else echo "There is no persistent storage, so the new boot animation is lost when shutting down"; fi

hat: main.c hat.c hat.h
	$(CC) $(CFLAGS) -o $@ main.c hat.c -lm

preview: preview.c hat.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl3) -o $@ preview.c $(shell pkg-config --libs sdl3)

run: hat preview
	./preview

uninstall:
	doas rm -f $(DEST)

clean:
	rm -f hat preview

.PHONY: install run uninstall clean
