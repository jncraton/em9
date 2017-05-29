all: em9

em9: src/em9.c
	gcc -O0 src/em9.c -o em9

static: src/em9.c
	gcc -O3 -static src/em9.c -o em9
	strip --strip-all em9

release: src/em9.c
	gcc -O3 -static src/em9.c -o em9
	strip --strip-all em9
			