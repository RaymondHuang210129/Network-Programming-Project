#include <cstdlib>
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <iterator>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "npshell.cpp"

#define QLEN 5

using namespace std;

void signalHandlerMain(int signum) {
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0) {}
	return;
}

int passiveSock(char* port, char* protocol, int qlen) {
	struct servent* serviceInfo;
	struct protoent* protocolInfo;
	struct sockaddr_in endpointAddr;
	int sockDescriptor;
	int sockType;
	bzero((char*)&endpointAddr, sizeof(endpointAddr));
	endpointAddr.sin_family = AF_INET;
	endpointAddr.sin_addr.s_addr = INADDR_ANY;
	endpointAddr.sin_port = htons((u_short)atoi(port));
	if (endpointAddr.sin_port == 0) {
		cerr << "invalid port number." << endl;
		exit(-1);
	}
	protocolInfo = getprotobyname(protocol);
	if (protocolInfo == 0) {
		cerr << "invalid protocol." << endl;
		exit(-1);
	}
	if (strcmp(protocol, "udp") == 0) {
		sockType = SOCK_DGRAM;
	} else if (strcmp(protocol, "tcp") == 0) {
		sockType = SOCK_STREAM;
	} else {
		cerr << "invalid sockType." << endl;
		exit(-1);
	}
	sockDescriptor = socket(PF_INET, sockType, protocolInfo->p_proto);
	if (sockDescriptor < 0) {
		cerr << "cannot create socket due to errno " << errno << endl;
		exit(-1);
	}
	if (bind(sockDescriptor, (struct sockaddr*)&endpointAddr, sizeof(endpointAddr)) < 0) {
		cerr << "cannot bind to port " << port << " " << errno << endl;
		exit(-1);
	}
	if (sockType == SOCK_STREAM && listen(sockDescriptor, qlen) < 0) {
		cerr << "cannot listen on port " << port << " " << errno << endl;
	}
	return sockDescriptor;
}

//passivesock(port, "tcp", qlen);
//passivesock(port, "udp", 0);

int main(int argc, char** argv, char** envp) {
	if (argc != 2) {
		cerr << "wrong argument" << endl;
		exit(0);
	}
	char* port = argv[1];
	struct sockaddr_in clientAddr;
	int masterSock, slaveSock;
	socklen_t clientAddrLen = sizeof((struct sockaddr*) &clientAddr);
	signal(SIGCHLD, signalHandlerMain);
	char* protocol = "tcp";
	masterSock = passiveSock(port, protocol, QLEN);
	while(1) {
		slaveSock = accept(masterSock, (struct sockaddr*)&clientAddr, &clientAddrLen);
		if (slaveSock < 0) {
			if (errno == EINTR) continue;
			cerr << "cannot accept: " << errno << endl;
			exit(-1); 
		}
		pid_t cpid;
		while((cpid = fork()) == -1) {};
		if (cpid == 0) { /* note: child */
			close(masterSock);
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			dup2(slaveSock, STDIN_FILENO);
			dup2(slaveSock, STDOUT_FILENO);
			dup2(slaveSock, STDERR_FILENO);
			close(slaveSock);
			npshell();
			exit(0);
		} else { /* note: parent */
			close(slaveSock);
		}
	}
	return 0;
}