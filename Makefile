MINIFB_DIR = lib/minifb
MINIFB_INC = -I$(MINIFB_DIR)/include
MINIFB_LIB = $(MINIFB_DIR)/build/libminifb.a
PLATFORM_LIBS = -lX11 -lGL -lpthread
FILES = $(shell find . -maxdepth 1 -name "*.c")

all: $(MINIFB_LIB)
	gcc --debug -lm $(MINIFB_INC) -o bin/typeking $(FILES) $(MINIFB_LIB) $(PLATFORM_LIBS)

$(MINIFB_LIB):
	cd $(MINIFB_DIR) && mkdir -p build && cd build && cmake .. && make

run: all
	bin/typeking

release: $(MINIFB_LIB)
	gcc -lm $(MINIFB_INC) -O3 -o bin/typeking $(FILES) $(MINIFB_LIB) $(PLATFORM_LIBS)

clean:
	rm -f bin/typeking
