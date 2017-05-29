all: em9

em9: src/em9.c
	gcc -O0 src/em9.c -o em9
