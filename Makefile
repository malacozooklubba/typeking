SDL_FLAGS = $(shell pkg-config --cflags --libs sdl3)

all:
	gcc --debug $(SDL_FLAGS) -o bin/main main.c

run: all
	SDL_VIDEODRIVER=x11 bin/main

release:
	gcc $(SDL_FLAGS) -O3 -o bin/main main.c

clean:
	rm -f main
