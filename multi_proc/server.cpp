
#include "header.hpp"

#define QLEN 5
#define SHMINFOKEY 54089588
#define SHMSIGARGKEY 37063333
#define SEMSIGARGKEY 46804706

using namespace std;

static int statglb_shmInfoID;
static int statglb_shmSigArgID;
static int statglb_semSigArgID;

void SIGCHLDHandlerMain(int signum) {
	int status;
	pid_t cpid;
	key_t shmInfoKey = (key_t)SHMINFOKEY;
	int statglb_shmInfoID = shmget(shmInfoKey, sizeof(UserInfo) * 30, 0644|IPC_CREAT);
	while(cpid = waitpid(-1, &status, WNOHANG) > 0) {
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0); 
		for (int i = 0; i < 30; i++) {
			if (userInfo[i].processID == cpid) {
				userInfo[i].used = false;
				strcpy(userInfo[i].userName, "(no name)");
				userInfo[i].processID = 0;
				userInfo[i].endpointAddr = NULL;
				break;
			}
		}
		shmdt(userInfo);
	}
	return;
}

void SIGINTHandlerMain(int signum) {
	int status;
	cout << "clean up shm and sem" << endl;
	del_sem(statglb_semSigArgID);
	shmctl(statglb_shmInfoID, IPC_RMID, 0);
	shmctl(statglb_shmSigArgID, IPC_RMID, 0);
	exit(0);
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

int main(int argc, char** argv, char** envp) {
	if (argc != 2) {
		cerr << "wrong argument" << endl;
		exit(0);
	}
	char* port = argv[1];
	struct sockaddr_in clientAddr;
	int masterSock, slaveSock;
	socklen_t clientAddrLen = sizeof((struct sockaddr*) &clientAddr);
	char protocol[4] = "tcp";
	masterSock = passiveSock(port, protocol, QLEN); 
	/* note: set signal handler */
	signal(SIGCHLD, SIGCHLDHandlerMain);
	signal(SIGINT, SIGINTHandlerMain);
	/* note: create share memory for 30 users' info */
	statglb_shmInfoID = shmget((key_t)SHMINFOKEY, sizeof(UserInfo) * 30, 0644|IPC_CREAT);
	if (statglb_shmInfoID == -1) {cerr << "statglb_shmInfoID error " << errno << endl; exit(-1);}
	UserInfo* tmpUserInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	memset(tmpUserInfo, 0, sizeof(UserInfo) * 30);
	shmdt(tmpUserInfo);
	/* note: create share memory for signal parameter */
	statglb_shmSigArgID = shmget((key_t)SHMSIGARGKEY, sizeof(SIGUSR1Info), 0644|IPC_CREAT);
	if (statglb_shmSigArgID == -1) {cerr << "statglb_shmSigArgID error " << errno << endl; exit(-1);}
	SIGUSR1Info* tmpSIGUSR1Info = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	memset(tmpSIGUSR1Info, 0, sizeof(SIGUSR1Info));
	shmdt(tmpSIGUSR1Info);
	/* note: create semaphore to garuntee the ShmSigArg RW correctness */
	statglb_semSigArgID = init_sem((key_t)SEMSIGARGKEY, 0);	
	if (statglb_semSigArgID == -1) {cerr << "statglb_semSigArgID error " << errno << endl; exit(-1);}
	/* note: start to wait for connections */
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
			npshell(getppid(), statglb_shmInfoID, statglb_shmSigArgID, statglb_semSigArgID, clientAddr);
			exit(0);
		} else { /* note: parent */
			close(slaveSock);
		}
	}
	return 0;
}