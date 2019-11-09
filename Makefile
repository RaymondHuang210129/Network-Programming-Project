all:
	g++ ./simple/server.cpp -o np_simple
	g++ ./multi_proc/server.cpp ./multi_proc/npshell.cpp ./multi_proc/sem.cpp -o np_multi_proc
clean:
	rm ./np_simple
	em ./np_multi_proc
