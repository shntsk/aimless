CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = sdl2 SDL2_image SDL2_mixer SDL2_ttf
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm
SRC      = $(wildcard src/*.c)

aimless: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

clean:
	rm -f aimless

.PHONY: clean install

PREFIX ?= /usr/local
install: aimless
	install -Dm755 aimless $(DESTDIR)$(PREFIX)/bin/aimless
