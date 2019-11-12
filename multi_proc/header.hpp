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
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define UNICAST 1000
#define BROADCAST 1001

#define LOGIN 2000
#define LOGOUT 2001
#define RENAME 2002
#define YELL 2003
#define TELL 2004
#define PIPESEND 2005
#define PIPERECV 2006

#define PIPETONULL 3000

using namespace std;

struct UserInfo {
	bool used = false;
	char userName[25] = "(no name)";
	struct sockaddr_in endpointAddr;
	pid_t processID;
};

struct SIGUSR1Info {
	int castMode = BROADCAST;
	int signalMode = LOGIN;
	char message[1030];
	int senderIndex = 0;
	int receiverIndex = 0;
};

struct NamepipeProcessBind {
	bool used;
	short senderIndex;
	short receiverIndex;
};

union semun {
	int val;    /* Value for SETVAL */
	struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
	unsigned short *array;  /* Array for GETALL, SETALL */
	struct seminfo *__buf;  /* Buffer for IPC_INFO
                                           (Linux-specific) */
};

int npshell(pid_t ppid, int shmInfoID, int shmSigArgID, int semSigArgID, int statglb_shmPipeProcBindID, sockaddr_in clientAddr);

int init_sem(key_t key, int semVal);

int del_sem(int semID);

int v_sem(int semID, int value);

int p_sem(int semID, int value);