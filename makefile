all: se

se: src/edit.c
	gcc -O0 src/edit.c -o se