SDL_FLAGS = $(shell pkg-config --cflags --libs sdl3)
FILES = $(shell find . -name "*.c")

all:
	gcc --debug $(SDL_FLAGS) -o bin/main $(FILES)

run: all
	SDL_VIDEODRIVER=x11 bin/main

macos-run: all
	SDL_RENDER_DRIVER=metal bin/main

release:
	gcc $(SDL_FLAGS) -O3 -o bin/main $(FILES)

clean:
	rm -f main
