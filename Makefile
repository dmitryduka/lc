all: main vm

main: main.cc
	g++ -std=c++11 -g main.cc -o main
vm: vm.cc
	mkdir -p build
	g++ -std=c++11 -c -I/usr/local/include -g -fno-exceptions vm.cc -o build/vm.o
	g++ build/vm.o /usr/local/lib/libjit.a -lpthread -o vm

clean:
	-rm main vm
graph:
	dot -Tpng graph.txt > graph.png
