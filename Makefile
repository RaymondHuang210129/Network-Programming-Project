all:
	clang++ server.cpp -o server -std=c++11 -Wall -pthread -lboost_system -lboost_regex
	clang++ console.cpp -o console.cgi -std=c++11 