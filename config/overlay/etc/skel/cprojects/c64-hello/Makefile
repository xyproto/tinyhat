hello.prg: hello.c
	cl65 -t c64 -Oirs -o $@ hello.c

run: hello.prg
	x64sc -autostart hello.prg

clean:
	rm -f hello.prg hello.o

.PHONY: run clean
