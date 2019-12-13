CXXFLAGS = -std=c++11 -I/usr/lib/llvm-6.0/include

all: orec

orec: main.o json.o
	g++ $^ -o $@ -L/usr/lib/llvm-6.0/lib `/usr/bin/llvm-config-6.0 --libs`

clean:
	rm -f orec
	rm -f *.o
	rm -f a.out

run: orec
	./orec >test.ll
	clang test.ll
	./a.out

