WITHJIT=0

all: main vm

main: main.cc
	g++ -std=c++11 -g main.cc -o main
vm: vm.cc
	mkdir -p build
ifeq ($(WITHJIT),1)
	g++ -DWITH_JIT=1 -std=c++11 -c -I/usr/local/include -O3 -fno-exceptions vm.cc -o build/vm.o
	g++ build/vm.o /usr/local/lib/libjit.a -lpthread -o vm
else
	g++ -DWITH_JIT=0 -std=c++11 -c -I/usr/local/include -O3 -fno-exceptions vm.cc -o build/vm.o
	g++ build/vm.o -lpthread -o vm
endif

clean:
	-rm main vm
graph:
	dot -Tpng graph.txt > graph.png
