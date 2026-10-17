CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = sdl2 SDL2_image SDL2_mixer SDL2_ttf
# kept separate from CFLAGS so `make CFLAGS=...` on the command line still finds SDL
SDL_CFLAGS = $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm
SRC      = $(wildcard src/*.c)

aimless: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SRC) -o $@ $(LDLIBS)

# skin discovery tests (parser, file lookup, .osk import, circle-number layout) + loader test on a fake SDL; no SDL needed. OSK=/path/to/skin.osk adds a real-skin run
OSK ?=
test: tests/test_skin tests/test_skin_loader
	./tests/test_skin $(if $(OSK),"$(OSK)")
	./tests/test_skin_loader $(if $(OSK),"$(OSK)")

tests/test_skin: tests/test_skin.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) tests/test_skin.c src/paths.c src/skinfs.c src/skinini.c src/beatmap.c -o $@ -lm

# runs the real src/skin.c against a fake SDL (tests/sdlfake): checks which files the circle digits load from, not rendering
tests/test_skin_loader: tests/test_skin_loader.c src/skin.c src/skinfs.c src/skinini.c tests/sdlfake/fake.c $(wildcard src/*.h) $(wildcard tests/sdlfake/*.h)
	$(CC) $(CFLAGS) -Itests/sdlfake tests/test_skin_loader.c src/skin.c src/skinfs.c src/skinini.c tests/sdlfake/fake.c -o $@ -lm

clean:
	rm -f aimless tests/test_skin tests/test_skin_loader

PREFIX ?= /usr/local
install: aimless
	install -Dm755 aimless $(DESTDIR)$(PREFIX)/bin/aimless

.PHONY: clean install test
