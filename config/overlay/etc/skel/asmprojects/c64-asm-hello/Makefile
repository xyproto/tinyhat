hello.prg: hello.s
	cl65 -t c64 -C c64-asm.cfg -u __EXEHDR__ -o $@ hello.s

run: hello.prg
	x64sc -autostart hello.prg

clean:
	rm -f hello.o hello.prg

.PHONY: run clean
