LINKS = -lSDL2 -lSDL2_image

main: main.c
	gcc $^ -o $@ $(LINKS)
