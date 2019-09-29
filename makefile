all: em9

DEPS=src/keyboard.h src/keyboard.o src/main.o
CC_FLAGS=

makeheaders: src/makeheaders.c
	gcc -O0 src/makeheaders.c -o makeheaders

%.h: %.c makeheaders
	./makeheaders $<

%.o: %.c
	gcc $(CC_FLAGS) -c $< -o $@

em9-debug: $(DEPS)
	gcc $(CC_FLAGS) -O0 $^ -o em9

em9-static: $(DEPS)
	gcc $(CC_FLAGS) -Os -static $^ -o em9-static
	du -b em9-static
	strip --strip-all em9-static
	du -b em9-static

em9: $(DEPS)
	gcc -O3 $(CC_FLAGS) $^ -o em9
	du -b em9
	strip --strip-all em9
	du -b em9

test: em9
	rm -f test/1.txt
	touch test/1.txt
	expect test/1
	cmp -s test/1.txt test/output1.txt
	rm -f test/1.txt

install: em9
	gcc -O3 src/main.c -o em9
	mv em9 /usr/local/bin/	

clean:
	rm -f em9*
	rm -f src/*.h
	rm -f src/*.o
	rm -f makeheaders