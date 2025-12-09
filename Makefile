CFLAGS = -Wall -Wextra -std=c99
SDL_FLAGS = $(shell pkg-config --cflags --libs sdl3)

all:
	gcc $(CFLAGS) $(SDL_FLAGS) -o bin/square_game square_game.c

release:
	gcc $(CFLAGS) $(SDL_FLAGS) -O3 -o bin/square_game square_game.c

clean:
	rm -f main square_game
