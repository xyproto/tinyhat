hello.ch8: hello.8o
	octo-cli hello.8o hello.ch8
	@echo "$@ is $$(wc -c < $@) bytes"

run: hello.ch8
	octo-run hello.ch8

clean:
	rm -f hello.ch8

.PHONY: run clean
