SDL_FLAGS = $(shell pkg-config --cflags --libs sdl3)
FILES = $(shell find . -name "*.c")

all:
	gcc --debug -lm $(SDL_FLAGS) -o bin/typeking $(FILES)

run: all
	SDL_VIDEODRIVER=x11 bin/typeking

macos-run: all
	SDL_RENDER_DRIVER=metal bin/typeking

release:
	gcc -lm $(SDL_FLAGS) -O3 -o bin/typeking $(FILES)

clean:
	rm -f main
