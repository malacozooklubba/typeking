SDL_FLAGS = $(shell pkg-config --cflags --libs sdl3)
FILES = $(shell find . -name "*.c")

all:
	gcc --debug -lm $(SDL_FLAGS) -o bin/main $(FILES)

linux:
	gcc --debug -o bin/main ./main.c ./fps_counter.c -lSDL3 -lm

run: all
	SDL_VIDEODRIVER=x11 bin/main

macos-run: all
	SDL_RENDER_DRIVER=metal bin/main

release:
	gcc -lm $(SDL_FLAGS) -O3 -o bin/main $(FILES)

clean:
	rm -f main
