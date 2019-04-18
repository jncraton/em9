all: em9

makeheaders: src/makeheaders.c
	gcc -O0 src/makeheaders.c -o makeheaders

%.h: %.c makeheaders
	./makeheaders $<

%.o: %.c
	gcc -Wall -Wextra -c $< -o $@

em9: src/keyboard.h src/keyboard.o src/main.o
	gcc -O0 -Wall -Wextra $^ -o em9

release: em9
	du -b em9
	strip --strip-all em9
	du -b em9

static: src/main.c
	gcc -Os -Wall -Wextra -static src/main.c -o em9-static
	du -b em9-static
	strip --strip-all em9-static
	du -b em9-static

test: em9
	rm -f test/1.txt
	touch test/1.txt
	expect test/1
	cmp -s test/1.txt test/output1.txt
	rm -f test/1.txt

install: release
	cp em9 /usr/local/bin/	
			
clean:
	rm -f em9*
	rm -f src/*.h
	rm -f makeheaders