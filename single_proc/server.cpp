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
#include <map>

#define QLEN 30
#define PIPETONULL 3000

using namespace std;

struct ExecCmd {
	string cmd;
	int pipeToCmd;
	bool errRedir;
	vector<string> args;
	int userPipeOut = 0;
	int userPipeIn = 0;
};

struct UserInfo {
	bool used = false;
	char userName[25] = "(no name)";
	struct sockaddr_in endpointAddr;
	int fd = -1;
	map<string, string> env;
};

struct UserPipeInfo {
	int senderIndex;
	int receiverIndex;
	int fd[2];
};

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
	int bReuseaddr = 1;
	if (setsockopt(sockDescriptor, SOL_SOCKET, SO_REUSEADDR, &bReuseaddr, sizeof(int)) == -1) {
		cerr << "cannot setsockopt " << errno << endl;
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

void attachFd(int fd) {
	dup2(STDIN_FILENO, getdtablesize() - 3);
	dup2(STDOUT_FILENO, getdtablesize() - 2);
	dup2(STDERR_FILENO, getdtablesize() - 1);
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
}

void detachFd() {
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	dup2(getdtablesize() - 3, STDIN_FILENO);
	dup2(getdtablesize() - 2, STDOUT_FILENO);
	dup2(getdtablesize() - 1, STDERR_FILENO);
	close(getdtablesize() - 3);
	close(getdtablesize() - 2);
	close(getdtablesize() - 1);
}

void attachEnv(vector<UserInfo> userInfo, int userIndex) {
	clearenv();
	for(map<string, string>::iterator iter = userInfo[userIndex].env.begin(); iter != userInfo[userIndex].env.end(); iter++) {
		setenv(iter->first.c_str(), iter->second.c_str(), 1);
	}
}

int getUserIndex(vector<UserInfo> userInfo, int fd) {
	for (int i = 0; i < userInfo.size(); i++) {
		if (userInfo[i].used == true && userInfo[i].fd == fd) {
			return i;
		}
	}
	return -1;
}

void login(vector<UserInfo> &userInfo, int userIndex) {
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			if (i == userIndex) {
				cout << "****************************************" << endl;
				cout << "** Welcome to the information server. **" << endl;
				cout << "****************************************" << endl;
				cout << "*** User '" << userInfo[userIndex].userName 
					 << "' entered from " 
					 << inet_ntoa(userInfo[userIndex].endpointAddr.sin_addr) 
					 << ":" << userInfo[userIndex].endpointAddr.sin_port << ". ***" << endl;
				cout << "% " << flush;
			} else {
				cout << "*** User '" << userInfo[userIndex].userName 
					 << "' entered from " 
					 << inet_ntoa(userInfo[userIndex].endpointAddr.sin_addr) 
					 << ":" << userInfo[userIndex].endpointAddr.sin_port << ". ***" << endl;
			}
			detachFd();
		}
	}
}

void logout(vector<UserInfo> &userInfo, int fd, vector<UserPipeInfo> &createdUserPipe) {
	char userName[25];
	int userIndex = getUserIndex(userInfo, fd);
	userInfo[userIndex].used = false;
	strcpy(userName, userInfo[userIndex].userName);
	strcpy(userInfo[userIndex].userName, "(no name)");
	userInfo[userIndex].fd = -1;
	userInfo[userIndex].env.clear();
	for (int i = 0; i < createdUserPipe.size(); i++) {
		if (createdUserPipe[i].senderIndex == userIndex || createdUserPipe[i].receiverIndex == userIndex) {
			close(createdUserPipe[i].fd[0]);
			close(createdUserPipe[i].fd[1]);
			createdUserPipe.erase(createdUserPipe.begin() + i, createdUserPipe.begin() + (i + 1));
			i--;
		}
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			cout << "*** User '" << userName << "' left. ***" << endl;
			detachFd();
		}
	}
}

void who(vector<UserInfo> &userInfo, int fd) {
	cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			cout << i + 1 << "\t" << userInfo[i].userName 
				 << "\t" << inet_ntoa(userInfo[i].endpointAddr.sin_addr) 
				 << ":" << userInfo[i].endpointAddr.sin_port;
			if (userInfo[i].fd == fd) {
				cout << "\t<-me" << endl;
			} else {
				cout << endl;
			}
		}
	}
	cout << "% " << flush;
}

void rename(vector<UserInfo> &userInfo, int fd, string name) {
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true && strcmp(name.c_str(), userInfo[i].userName) == 0) {
			attachFd(fd);
			cout << "*** User '" << name <<"' already exists. ***" << endl;
			cout << "% " << flush;
			detachFd();
			return;
		}
	}
	string message;
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true && userInfo[i].fd == fd) {
			strcpy(userInfo[i].userName, name.c_str());
			string address(inet_ntoa(userInfo[i].endpointAddr.sin_addr)); 
			message = "*** User from " + address +
			          ":" + to_string(userInfo[i].endpointAddr.sin_port) +
			          " is named '" + userInfo[i].userName + "'. ***";
			break;
		}
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			cout << message << endl;
			if (userInfo[i].fd == fd) {
				cout << "% " << flush;
			}
			detachFd();
		}
	}
}

void yell(vector<UserInfo> &userInfo, int fd, vector<string> splitedCmd) {
	int senderIndex;
	string message = splitedCmd[1];
	for (int i = 2; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true && userInfo[i].fd == fd) {
			senderIndex = i;
			break;
		}
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			cout << "*** " << userInfo[senderIndex].userName 
			     << " yelled ***: " << message << endl;
			if (userInfo[i].fd == fd) {
				cout << "% " << flush;
			}
			detachFd();
		}
	}
}

void tell(vector<UserInfo> &userInfo, int fd, vector<string> splitedCmd) {
	string message = splitedCmd[2];
	int senderIndex;
	for (int i = 3; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	for(int i = 0; i < 30; i++) {
		if (userInfo[i].used == true && userInfo[i].fd == fd) {
			senderIndex = i;
			break;
		}
	}
	if (userInfo[stoi(splitedCmd[1]) - 1].used == true) {
		attachFd(userInfo[stoi(splitedCmd[1]) - 1].fd);
		cout << "*** " << userInfo[senderIndex].userName 
		     << " told you *** : " << message << endl;
		detachFd();
	} else {
		attachFd(fd);
		cout << "*** Error: user #" << splitedCmd[1] << " does not exist yet. ***" << endl;
		detachFd();
	}
	attachFd(fd);
	cout << "% " << flush;
	detachFd();
}

void pipeOut(vector<UserInfo> userInfo, UserPipeInfo pipeInfo, vector<string> splitedCmd) {
	string message = splitedCmd[0];
	for (int i = 1; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			cout << "*** " << userInfo[pipeInfo.senderIndex].userName
			     << " (#" << pipeInfo.senderIndex + 1 
			     << ") just piped '" << message 
			     << "' to " << userInfo[pipeInfo.receiverIndex].userName
			     << " (#" << pipeInfo.receiverIndex + 1 << ") ***" << endl;
			detachFd();
		}
	}
}

void pipeIn(vector<UserInfo> userInfo, UserPipeInfo pipeInfo, vector<string> splitedCmd) {
	string message = splitedCmd[0];
	for (int i = 1; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			attachFd(userInfo[i].fd);
			cout << "*** " << userInfo[pipeInfo.receiverIndex].userName 
    			 << " (#" << pipeInfo.receiverIndex + 1 
	    		 << ") just received from " << userInfo[pipeInfo.senderIndex].userName 
		    	 << " (#" << pipeInfo.senderIndex + 1 
			     << ") by '" << message << "' ***" << endl;
			detachFd();
		}
	}
}

int main(int argc, char** argv, char** envp) {
	if (argc != 2) {
		cerr << "wrong argument" << endl;
		exit(0);
	}
	vector<vector<vector<string>>> userEnv(getdtablesize(), vector<vector<string>>(0));
	char* port = argv[1];
	struct sockaddr_in clientAddr;
	int masterSock;
	socklen_t clientAddrLen = sizeof((struct sockaddr*) &clientAddr);
	char protocol[4] = "tcp";
	fd_set readFdSet, activeFdSet;
	masterSock = passiveSock(port, protocol, QLEN); 
	FD_ZERO(&activeFdSet);
	FD_SET(masterSock, &activeFdSet);	
	clearenv();
	setenv("PATH", "bin:.", 1);
	signal(SIGCHLD, signalHandlerMain);
	vector<vector<vector<int>>> createdPipesToEachCmdVec(getdtablesize(), vector<vector<int>>(0)); /* note: register each command's created read Pipe (and another end) */
	vector<vector<vector<int>>> assignedEntriesToEachCmdVec(getdtablesize(), vector<vector<int>>(0)); /* note: in and out direction will be assigned and then can be closed by main proccess later */
	vector<UserPipeInfo> createdUserPipe(0);
	vector<UserInfo> userInfo(30);
	while(1) {
		memcpy(&readFdSet, &activeFdSet, sizeof(readFdSet));
		if (select(getdtablesize(), &readFdSet, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0) {
			if (errno == EINTR) {continue;}
			cerr << "select error " << errno << endl;
			exit(-1);
		}
		/* section: accept new login user */
		if (FD_ISSET(masterSock, &readFdSet)) {
			int slaveSock = accept(masterSock, (struct sockaddr*)&clientAddr, &clientAddrLen);
			if (slaveSock < 0) {
				cerr << "accept error " << errno << endl;
				exit(-1);
			}
			FD_SET(slaveSock, &activeFdSet);
			int newUserFd;
			for (int i = 0; i < 30; i++) {
				if (userInfo[i].used == false) {
					userInfo[i].used = true;
					strcpy(userInfo[i].userName, "(no name)");
					userInfo[i].endpointAddr = clientAddr;
					userInfo[i].fd = slaveSock;
					newUserFd = slaveSock;
					pair<string, string> newEnv("PATH", ".");
					userInfo[i].env.insert(newEnv);
					login(userInfo, i);
					break;
				}
			}
			//backupEnv(envp, slaveSock, userEnv);
		}
		/* section: dup original stdin, stdout, stderr to getsize()-3, -2, -1 */
		/* section: serve existed user */
		for (int fd = 0; fd < getdtablesize() - 3; fd++) {
			if (fd != masterSock && FD_ISSET(fd, &readFdSet)) {
				attachFd(fd);
				attachEnv(userInfo, getUserIndex(userInfo, fd));
				//restoreEnv(fd, userEnv);
				//printenv(fd, userEnv);
				string rawCmd;
				vector<ExecCmd> cmdList;
				if (!getline(cin, rawCmd)) {
					/* todo: */
				}
				istringstream iss(rawCmd);
				vector<string> splitedCmd(istream_iterator<string>{iss}, 
				                          istream_iterator<string>());
				/* section: parse */
				int promptCmdCounter = 0;
				string oFile = "";
				for (int i = 0, argFlag = 0; i < splitedCmd.size(); i++) {
					if (splitedCmd[i] == "|") { /* note: regular pipe */
						cmdList[cmdList.size() - 1].pipeToCmd = 1;
						argFlag = 0;
					}
					else if (splitedCmd[i].length() > 1 && 
							splitedCmd[i].substr(0, 1) == "|" && 
							stoi(splitedCmd[i].substr(1)) >= 1 && 
							stoi(splitedCmd[i].substr(1)) <= 1000) { /* note: hop pipe */
						cmdList[cmdList.size() - 1].pipeToCmd = stoi(splitedCmd[i].substr(1));
						argFlag = 0;
					}
					else if (splitedCmd[i].length() > 1 && 
							splitedCmd[i].substr(0, 1) == "!" && 
							stoi(splitedCmd[i].substr(1)) >= 1 && 
							stoi(splitedCmd[i].substr(1)) <= 1000) { /* note: err pipe */
						cmdList[cmdList.size() - 1].pipeToCmd = stoi(splitedCmd[i].substr(1));
						cmdList[cmdList.size() - 1].errRedir = true;
						argFlag = 0;
					}
					else if (splitedCmd[i] == ">") { /* note: output to file */
						oFile = splitedCmd[i + 1];
						break;
					}
					else if (splitedCmd[i].length() > 1 && 
							splitedCmd[i].substr(0, 1) == ">" && 
							stoi(splitedCmd[i].substr(1)) >= 1 && 
							stoi(splitedCmd[i].substr(1)) <= 30) { /* note: cross-users pipe */
						//userPipeOut = stoi(splitedCmd[i].substr(1));
						cmdList[cmdList.size() - 1].userPipeOut = stoi(splitedCmd[i].substr(1));
						argFlag = 0;
					}
					else if (splitedCmd[i].length() > 1 && 
							splitedCmd[i].substr(0, 1) == "<" && 
							stoi(splitedCmd[i].substr(1)) >= 1 && 
							stoi(splitedCmd[i].substr(1)) <= 30) { /* note: cross-users pipe */
						//userPipeIn = stoi(splitedCmd[i].substr(1));
						cmdList[cmdList.size() - 1].userPipeIn = stoi(splitedCmd[i].substr(1));
						argFlag = 0;
					}
					else { /* note: command */
						if (argFlag) { /* argument */
							cmdList[cmdList.size() - 1].args.push_back(splitedCmd[i]);
						}
						else { /* program */
							argFlag = 1;
							vector<string> tmpVec(1, splitedCmd[i]);
							ExecCmd execCmd = {splitedCmd[i], 0, false, tmpVec, 0, 0};
							cmdList.push_back(execCmd);
							promptCmdCounter++;
						}	
					}
				}
				if (splitedCmd.size() > 0 && splitedCmd[0] == "setenv") {
					setenv(splitedCmd[1].c_str(), splitedCmd[2].c_str(), 1);
					if (userInfo[getUserIndex(userInfo, fd)].env.find(splitedCmd[1]) != userInfo[getUserIndex(userInfo, fd)].env.end()) {
						userInfo[getUserIndex(userInfo, fd)].env[splitedCmd[1]] = splitedCmd[2];
					} else {
						userInfo[getUserIndex(userInfo, fd)].env.insert(pair<string, string>(splitedCmd[1], splitedCmd[2]));
					}
					cmdList.erase(cmdList.begin());
					cout << "% " << flush;
					continue;
				}
				if (splitedCmd.size() == 1 && splitedCmd[0] == "who") {
					who(userInfo, fd);
					detachFd();
					cmdList.erase(cmdList.begin());
					continue;
				}
				if (splitedCmd.size() == 2 && splitedCmd[0] == "name") {
					detachFd();
					rename(userInfo, fd, splitedCmd[1]);
					cmdList.erase(cmdList.begin());
					continue;
				}
				if (splitedCmd.size() >= 2 && splitedCmd[0] == "yell") {
					detachFd();
					yell(userInfo, fd, splitedCmd);
					cmdList.erase(cmdList.begin());
					continue;
				}
				if (splitedCmd.size() >= 3 && splitedCmd[0] == "tell") {
					detachFd();
					tell(userInfo, fd, splitedCmd);
					cmdList.erase(cmdList.begin());
					continue;
				}
				if (splitedCmd.size() == 1 && splitedCmd[0] == "exit") {
					/* section: close all personal pipes */
					for(int i = 0; i < createdPipesToEachCmdVec[fd].size(); i++) {
						if (createdPipesToEachCmdVec[fd][i][0] != 0) {
							close(createdPipesToEachCmdVec[fd][i][0]);
						}
						if (createdPipesToEachCmdVec[fd][i][1] != 0) {
							close(createdPipesToEachCmdVec[fd][i][1]);
						}
					}
					createdPipesToEachCmdVec[fd].resize(0);
					assignedEntriesToEachCmdVec[fd].resize(0);
					detachFd();
					logout(userInfo, fd, createdUserPipe);
					close(fd);
					FD_CLR(fd, &activeFdSet);
					continue;
				}
				vector<int> tmpPair(2, 0);
				if (assignedEntriesToEachCmdVec[fd].size() < promptCmdCounter) {
					assignedEntriesToEachCmdVec[fd].resize(promptCmdCounter, tmpPair);
				}
				if (createdPipesToEachCmdVec[fd].size() < promptCmdCounter) {
					createdPipesToEachCmdVec[fd].resize(promptCmdCounter, tmpPair);
				}
				/* section: create new pipe or use existed pipe */
//				for (int i = 0; i < promptCmdCounter; i++) {
//					/* check whether exists a pipe that should connect to rPipe */
//					if (createdPipesToEachCmdVec[fd][i][0] != 0) { /* note: exists a pipe connect to this command */
//						assignedEntriesToEachCmdVec[fd][i][0] = createdPipesToEachCmdVec[fd][i][0];
//					}
//					/* check whether need a pipe to send*/
//					if (cmdList[i].pipeToCmd != 0) {
//						if (createdPipesToEachCmdVec[fd].size() <= cmdList[i].pipeToCmd + i) {
//							createdPipesToEachCmdVec[fd].resize(cmdList[i].pipeToCmd + i + 1, tmpPair);
//						}
//						if (createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][0] == 0) { /* note: create new pipe for THAT cmd */
//							if (pipe(&createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][0]) == -1) {
//								cout << errno << endl;
//								exit(-1);
//							}
//						}
//						assignedEntriesToEachCmdVec[fd][i][1] = createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][1];
//					}
//				}
				/* section: check whether need to pipe to/from another user */
				for (int i = 0; i < promptCmdCounter; i++) {
					if (cmdList[i].userPipeOut != 0) {
						if (userInfo[cmdList[i].userPipeOut - 1].used == false) { /* user not exist: pipe to null */
							cout << "*** Error: user #" << cmdList[i].userPipeOut << " does not exist yet. ***" << endl;
							cmdList[i].userPipeOut = PIPETONULL;
						} else {
							bool isExist = false;
							int userIndex = getUserIndex(userInfo, fd);
							for (int j = 0; j < createdUserPipe.size(); j++) {
								if (createdUserPipe[j].senderIndex == userIndex && createdUserPipe[j].receiverIndex == (cmdList[i].userPipeOut - 1)) {
									cout << "*** Error: the pipe #" << userIndex + 1
									     << "->#" << cmdList[i].userPipeOut << " already exist. ***" << endl;
									cmdList[i].userPipeOut = PIPETONULL;
									isExist = true;
									break;
								}
							}
							if (isExist == false) {
								UserPipeInfo newPipe;
								newPipe.senderIndex = userIndex;
								newPipe.receiverIndex = cmdList[i].userPipeOut - 1;
								pipe(newPipe.fd);
								createdUserPipe.push_back(newPipe);
								detachFd();
								pipeOut(userInfo, newPipe, splitedCmd);
								attachFd(fd);
							}
						}
					}
					if (cmdList[i].userPipeIn != 0) {
						if (userInfo[cmdList[i].userPipeIn - 1].used == false) { /* user not exist: pipe to null */
							cout << "*** Error: user #" << cmdList[i].userPipeIn << " does not exist yet. ***" << endl;
							cmdList[i].userPipeIn = PIPETONULL;
						} else {
							int userIndex = getUserIndex(userInfo, fd);
							bool isExist = false;
							for (int j = 0; j < createdUserPipe.size(); j++) {
								if (createdUserPipe[j].senderIndex == cmdList[i].userPipeIn - 1 && createdUserPipe[j].receiverIndex == userIndex) {
									isExist = true;
									detachFd();
									pipeIn(userInfo, createdUserPipe[j], splitedCmd);
									attachFd(fd);
									break;
								}
							}
							if (isExist == false) {
								cout << "*** Error: the pipe #" << cmdList[i].userPipeIn 
								     << "->#" << userIndex + 1 << " does not exist yet. ***" << endl;
								cmdList[i].userPipeIn = PIPETONULL;
							}
						}
					}
				}
				vector<int> pidWaitList; /* note: some processes should be waited before printing % */
				for (int i = 0; i < promptCmdCounter; i++) {
					pid_t cpid;
					/* section: create new pipe or use existed pipe */
					/* check whether exists a pipe that should connect to rPipe */
					if (createdPipesToEachCmdVec[fd][i][0] != 0) { /* note: exists a pipe connect to this command */
						assignedEntriesToEachCmdVec[fd][i][0] = createdPipesToEachCmdVec[fd][i][0];
					}
					/* check whether need a pipe to send*/
					if (cmdList[i].pipeToCmd != 0) {
						if (createdPipesToEachCmdVec[fd].size() <= cmdList[i].pipeToCmd + i) {
							createdPipesToEachCmdVec[fd].resize(cmdList[i].pipeToCmd + i + 1, tmpPair);
						}
						if (createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][0] == 0) { /* note: create new pipe for THAT cmd */
							if (pipe(&createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][0]) == -1) {
								cout << errno << endl;
								exit(-1);
							}
						}
						assignedEntriesToEachCmdVec[fd][i][1] = createdPipesToEachCmdVec[fd][cmdList[i].pipeToCmd + i][1];
					}
					/* forking process */
					while((cpid = fork()) == -1) {}; /* note: busy waiting if process capacity exhausted */
					if (cpid == 0) { /* note: child */
						if (assignedEntriesToEachCmdVec[fd][i][0]) { /* note: stdin redirect */
							close(STDIN_FILENO);
							while(dup2(assignedEntriesToEachCmdVec[fd][i][0], STDIN_FILENO) == -1 && errno == EINTR);
						}
						if (assignedEntriesToEachCmdVec[fd][i][1]) { /* note: stdout redirect */
							close(STDOUT_FILENO);
							while(dup2(assignedEntriesToEachCmdVec[fd][i][1], STDOUT_FILENO) == -1 && errno == EINTR);
							if (cmdList[i].errRedir) { /* note: stderr redirect */
								close(STDERR_FILENO);
								while(dup2(assignedEntriesToEachCmdVec[fd][i][1], STDERR_FILENO) == -1 && errno == EINTR);
							}
						}
						/* section: attaching input user pipe */
						if (cmdList[i].userPipeIn == PIPETONULL) { /* note: invalid attempt to attach user pipe */
							int nullFd = open("/dev/null", O_RDONLY);
							close(STDIN_FILENO);
							dup2(nullFd, STDIN_FILENO);
							close(nullFd);
						} else if (cmdList[i].userPipeIn > 0) { /* note: valid attempt to attach user pipe */
							for (int j = 0; j < createdUserPipe.size(); j++) {
								int userIndex = getUserIndex(userInfo, fd);
								if (createdUserPipe[j].senderIndex == cmdList[i].userPipeIn - 1 && createdUserPipe[j].receiverIndex == userIndex) {
									close(STDIN_FILENO);
									dup2(createdUserPipe[j].fd[0], STDIN_FILENO);
									break;
								}
							}
						}
						/* section: attaching output user pipe */
						if (cmdList[i].userPipeOut == PIPETONULL) { /* note: invalid attempt to attach user pipe */
							int nullFd = open("/dev/null", O_WRONLY);
							close(STDOUT_FILENO);
							dup2(nullFd, STDOUT_FILENO);
							close(nullFd);
						} else if (cmdList[i].userPipeOut > 0) { /* note: valid attempt to attach user pipe */
							for (int j = 0; j < createdUserPipe.size(); j++) {
								int userIndex = getUserIndex(userInfo, fd);
								if (createdUserPipe[j].senderIndex == userIndex && createdUserPipe[j].receiverIndex == cmdList[i].userPipeOut - 1) {
									close(STDOUT_FILENO);
									dup2(createdUserPipe[j].fd[1], STDOUT_FILENO);
									break;
								}
							}
						}
						if (i == promptCmdCounter - 1) { /* note: the last command */
							if (oFile != "") { /* note: if the last command should output to file */
								int file = open(oFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
								close(STDOUT_FILENO);
								while(dup2(file, STDOUT_FILENO) == -1 && errno == EINTR);
							} 
						}
						for (int j = 3; j < getdtablesize(); j++) {close(j);} /* note: close all unused pipe entries */
						if (cmdList[i].cmd == "printenv") { 
							cout << getenv(cmdList[i].args[1].c_str()) << endl;
						}
						else { /* note: execute program */
							char* execVect[cmdList[i].args.size() + 1];
							for (int j = 0; j < cmdList[i].args.size(); j++) {
								execVect[j] = strdup(cmdList[i].args[j].c_str());
							}
							execVect[cmdList[i].args.size()] = NULL;
							execvp(execVect[0], execVect);
							if (errno == 2) {
								cerr << "Unknown command: [" << execVect[0] <<"]." << endl;
							}
							else {
								cerr << "exec error: " << errno << endl;
							}
						}
						exit(0);
					} else { /* note: parent */
						/* section: put commands that no need to hang to the wait list */
						if ((cmdList[i].userPipeOut == 0 || cmdList[i].userPipeOut == PIPETONULL) &&
						    cmdList[i].pipeToCmd + i < promptCmdCounter) {
							/* note: no pipeOut and hop pipe */
							pidWaitList.push_back(cpid);
						}
						/* section: parent close all pipes that do not connect to future commands */
						if (createdPipesToEachCmdVec[fd][i][0]) {
							close(createdPipesToEachCmdVec[fd][i][0]);
						}
						if (createdPipesToEachCmdVec[fd][i][1]) {
							close(createdPipesToEachCmdVec[fd][i][1]);
						}
						if (cmdList[i].userPipeOut > 0) {
							int userIndex = getUserIndex(userInfo, fd);
							for (int j = 0; j < createdUserPipe.size(); j++) {
								if (createdUserPipe[j].senderIndex == userIndex && createdUserPipe[j].receiverIndex == cmdList[i].userPipeOut - 1) {
									close(createdUserPipe[j].fd[1]);
									break;
								}
							}
						}
						if (cmdList[i].userPipeIn > 0) {
							int userIndex = getUserIndex(userInfo, fd);
							for (int j = 0; j < createdUserPipe.size(); j++) {
								if (createdUserPipe[j].senderIndex == cmdList[i].userPipeIn - 1 && createdUserPipe[j].receiverIndex == userIndex) {
									close(createdUserPipe[j].fd[0]);
									createdUserPipe.erase(createdUserPipe.begin() + j, createdUserPipe.begin() + (j + 1));
									break;
								}
							}
						}
					}
				}
				/* section: wait for processes in waitList */
				for (int i = 0; i < pidWaitList.size(); i++) {
					int status;
					pid_t cpid = waitpid(pidWaitList[i], &status, 0);
				}
				cmdList.erase(cmdList.begin(), cmdList.begin() + promptCmdCounter);
				createdPipesToEachCmdVec[fd].erase(createdPipesToEachCmdVec[fd].begin(), createdPipesToEachCmdVec[fd].begin() + promptCmdCounter);
				assignedEntriesToEachCmdVec[fd].erase(assignedEntriesToEachCmdVec[fd].begin(), assignedEntriesToEachCmdVec[fd].begin() + promptCmdCounter);
				cout << "% " << flush;
				//backupEnv(envp, fd, userEnv);
				detachFd();
			}
		}
	}
}