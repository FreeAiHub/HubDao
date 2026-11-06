# MiniCalc — збірка для Linux, macOS і Windows (крос-компіляція MinGW).
#   make            — збірка для поточної ОС (Linux або macOS)
#   make windows    — MiniCalc.exe через x86_64-w64-mingw32-gcc
#   make test       — юніт-тести ядра
CC      ?= cc
CFLAGS  ?= -std=c99 -Os -Wall -Wextra
BUILD   := build
UNAME_S := $(shell uname -s)
SRC     := src/core.c

ifeq ($(UNAME_S),Darwin)
all: mac
else
all: linux
endif

linux: $(BUILD)/minicalc
$(BUILD)/minicalc: $(SRC) src/plat_x11.c src/core.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) src/plat_x11.c -lX11
	strip $@

MAC_APP  := $(BUILD)/MiniCalc.app
MAC_ARCH ?= -arch arm64 -arch x86_64
mac: $(MAC_APP)/Contents/MacOS/MiniCalc
$(MAC_APP)/Contents/MacOS/MiniCalc: $(SRC) src/plat_cocoa.m src/core.h packaging/Info.plist
	@mkdir -p $(MAC_APP)/Contents/MacOS
	cp packaging/Info.plist $(MAC_APP)/Contents/Info.plist
	clang $(MAC_ARCH) -mmacosx-version-min=10.13 -Os -Wall -fobjc-arc \
	    -o $@ $(SRC) src/plat_cocoa.m -framework Cocoa
	strip -x $@
	codesign --force --sign - $(MAC_APP) 2>/dev/null || true

MINGW ?= x86_64-w64-mingw32-gcc
windows: $(BUILD)/MiniCalc.exe
$(BUILD)/MiniCalc.exe: $(SRC) src/plat_win32.c src/core.h
	@mkdir -p $(BUILD)
	$(MINGW) $(CFLAGS) -municode -mwindows -static-libgcc -s -o $@ $(SRC) src/plat_win32.c -lgdi32 -luser32

test: $(BUILD)/test_core
	./$(BUILD)/test_core
$(BUILD)/test_core: tests/test_core.c $(SRC) src/core.h
	@mkdir -p $(BUILD)
	$(CC) -std=c99 -Wall -Wextra -g -o $@ tests/test_core.c

clean:
	rm -rf $(BUILD)

.PHONY: all linux mac windows test clean
