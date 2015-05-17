all: lc vm
lc:
	g++ -std=c++11 -g main.cc -o main
vm:
	g++ -std=c++11 -g vm.cc -o vm

clean:
	-rm main vm
graph:
	dot -Tpng graph.txt > graph.png
