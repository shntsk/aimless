CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = sdl2 SDL2_image SDL2_mixer SDL2_ttf
# kept separate from CFLAGS so `make CFLAGS=...` on the command line still finds SDL
SDL_CFLAGS = $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm
SRC      = $(wildcard src/*.c)

aimless: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SRC) -o $@ $(LDLIBS)

clean:
	rm -f aimless

PREFIX ?= /usr/local
install: aimless
	install -Dm755 aimless $(DESTDIR)$(PREFIX)/bin/aimless

.PHONY: clean install
