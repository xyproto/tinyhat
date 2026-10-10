NAME = hat-in-the-stars
LOVE = 11.5
FILES = conf.lua main.lua game.fnl
FENNEL = $(firstword $(wildcard /usr/share/lua/5.4/fennel.lua /usr/share/lua/5.3/fennel.lua /usr/share/lua/5.1/fennel.lua fennel.lua))

run:
	love .

$(NAME).love: $(FILES)
	rm -f $@
	zip -9 -q $@ $(FILES)
	zip -9 -q -j $@ $(FENNEL)

win32 win64: $(NAME).love
	[ -f love-$(LOVE)-$@.zip ] || curl -fLO https://github.com/love2d/love/releases/download/$(LOVE)/love-$(LOVE)-$@.zip
	rm -rf $(NAME)-$@ $(NAME)-$@.zip
	unzip -q love-$(LOVE)-$@.zip
	mv love-$(LOVE)-$@ $(NAME)-$@
	cat $(NAME)-$@/love.exe $(NAME).love > $(NAME)-$@/$(NAME).exe
	rm -f $(NAME)-$@/love.exe $(NAME)-$@/lovec.exe $(NAME)-$@/love.ico $(NAME)-$@/changes.txt $(NAME)-$@/readme.txt
	zip -9 -q -r $(NAME)-$@.zip $(NAME)-$@
	@echo "$(NAME)-$@.zip is ready to be shared with Windows users"

clean:
	rm -rf $(NAME).love $(NAME)-win32 $(NAME)-win32.zip $(NAME)-win64 $(NAME)-win64.zip

.PHONY: run win32 win64 clean
