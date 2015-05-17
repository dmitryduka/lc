all: main vm

main: main.cc
	g++ -std=c++11 -g main.cc -o main
vm: vm.cc
	g++ -std=c++11 -g vm.cc -o vm

clean:
	-rm main vm
graph:
	dot -Tpng graph.txt > graph.png
