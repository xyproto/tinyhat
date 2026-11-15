hello.sfc: hello.s snes.cfg
	ca65 --cpu 65816 -o hello.o hello.s
	ld65 -C snes.cfg -o $@ hello.o
	sh checksum.sh $@

run: hello.sfc
	zsnes hello.sfc

clean:
	rm -f hello.o hello.sfc

.PHONY: run clean
