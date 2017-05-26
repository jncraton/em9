all: em9

em9: src/edit.c
	gcc -O0 src/edit.c -o em9