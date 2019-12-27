all:
	clang++ sock.cpp -o socks_server -std=c++11 -Wall -pthread -lboost_system -lboost_regex
