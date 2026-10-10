dos-intro.com: intro.asm
	nasm -f bin -o $@ intro.asm
	@echo "$@ is $$(wc -c < $@) bytes"

run: dos-intro.com
	dosbox-x -fastlaunch dos-intro.com -exit

clean:
	rm -f dos-intro.com

.PHONY: run clean
