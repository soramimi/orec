CXXFLAGS = -std=c++11 -I/usr/lib/llvm-3.5/include

all: orec

orec: main.o json.o
	g++ $^ -o $@ -L/usr/lib/llvm-3.5/lib `llvm-config-3.5 --libs` -lpthread -lncurses -ldl

clean:
	-rm orec
	-rm *.o

run:
	./orec >test.ll
	clang test.ll
	./a.out

#g++ -std=c++11 -I/usr/lib/llvm-3.5/include main.cpp -c -o orec.o
#g++ -std=c++11 -I/usr/lib/llvm-3.5/include json.cpp -c -o json.o
#g++ orec.o json.o -o orec -L/usr/lib/llvm-3.5/lib `llvm-config-3.5 --libs` -lpthread -lncurses -ldl
#rm test.ll
#./orec >test.ll
#clang test.ll
#./a.out

