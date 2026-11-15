export ROMNAME := hello
export ROMTITLE := TINY HAT SNES

include $(PVSNESLIB_HOME)/devkitsnes/snes_rules

.PHONY: all run

all: buildWithSummary

buildActual: $(OBJS) $(ROMNAME).sfc

run: all
	zsnes $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx cleanLogs
