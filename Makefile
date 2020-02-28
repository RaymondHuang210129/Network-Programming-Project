all:
	g++ ./simple/server.cpp -o np_simple
	g++ ./multi_proc/server.cpp ./multi_proc/npshell.cpp ./multi_proc/sem.cpp -o np_multi_proc
	g++ ./single_proc/server.cpp -o np_single_proc
clean:
	rm ./np_simple
	rm ./np_multi_proc
	rm ./np_single_proc
