CC = arm-none-eabi-gcc
CFLAGS = -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall
LDFLAGS = -specs=gba.specs

hello.gba: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o hello.elf main.c -ltonc
	arm-none-eabi-objcopy -O binary hello.elf $@
	gbafix -tTINYHAT $@

run: hello.gba
	mgba -3 hello.gba

clean:
	rm -f hello.elf hello.gba

.PHONY: run clean
