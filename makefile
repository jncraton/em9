all: em9

em9: src/em9.c
	gcc -O0 -Wall -Wextra src/em9.c -o em9

static: src/em9.c
	gcc -Os -Wall -Wextra -static src/em9.c -o em9-static
	strip --strip-all em9-static
	rm -f em9-static.xz
	xz -9e em9-static

release: src/em9.c
	gcc -Os -Wall -Wextra src/em9.c -o em9
	du -b em9
	strip --strip-all em9
	du -b em9
	rm -f em9.xz
	xz -9e em9
	du -b em9.xz

install: src/em9.c
	gcc -O3 src/em9.c -o em9
	mv em9 /usr/local/bin/	
			
clean:
	rm em9*