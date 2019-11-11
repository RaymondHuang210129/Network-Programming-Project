
#include "header.hpp"

using namespace std;

static int statglb_semSigArgID;
static int statglb_shmSigArgID;
static int statglb_shmInfoID;
static int statglb_userIndex;
static int statglb_pipeInFd[30];

struct ExecCmd {
	string cmd;
	int pipeToCmd;
	bool errRedir;
	vector<string> args;
};

/* grave the zombies */
void SIGCHLDHandlerShell(int sigNum) {
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0) {}
	return;
}

/* receive process signals */
void SIGUSR1HandlerShell(int sigNum) { 
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	//cout << "message: " << sigArg->message << "sender index: " << sigArg->senderIndex << endl;
	if (sigArg->signalMode == LOGIN) { 
		if (sigArg->senderIndex == statglb_userIndex) { 
			/* note: show welcome message if receive login signal from itself */
			cout << "****************************************" << endl;
			cout << "** Welcome to the information server. **" << endl;
			cout << "****************************************" << endl;
		}
		/* note: show login notification */
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0); 
		cout << "*** User '" << userInfo[sigArg->senderIndex].userName 
			 << "' entered from " 
			 << inet_ntoa(userInfo[sigArg->senderIndex].endpointAddr.sin_addr) 
			 << ":" << userInfo[sigArg->senderIndex].endpointAddr.sin_port << ". ***" << endl;
		shmdt(userInfo);
	} else if (sigArg->signalMode == LOGOUT) {
		/* note: show notification */
		cout << "*** User '" << sigArg->message << "' left. ***" << endl;
		/* section: close the input pipe that produced by terminated user */
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
		if (statglb_pipeInFd[sigArg->senderIndex] != 0) {
			close(statglb_pipeInFd[sigArg->senderIndex]);
			statglb_pipeInFd[sigArg->senderIndex] = 0;
		}
		shmdt(userInfo);
	} else if (sigArg->signalMode == RENAME) {
		/* note: show notification */
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0); 
		cout << "*** User from " 
			 << inet_ntoa(userInfo[sigArg->senderIndex].endpointAddr.sin_addr) 
			 << ":" << userInfo[sigArg->senderIndex].endpointAddr.sin_port 
			 << " is named '" << userInfo[sigArg->senderIndex].userName 
			 << "'. ***" << endl;
		shmdt(userInfo);
	} else if (sigArg->signalMode == YELL) {
		/* note: show notification */
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
		cout << "*** " << userInfo[sigArg->senderIndex].userName 
			 << " yelled ***: " << sigArg->message << endl;
		shmdt(userInfo);
	} else if (sigArg->signalMode == TELL) {
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
		cout << "*** " << userInfo[sigArg->senderIndex].userName 
			 << " told you *** : " << sigArg->message << endl;
		shmdt(userInfo);
	} else if (sigArg->signalMode == PIPESEND) {
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
		cout << "*** " << userInfo[sigArg->senderIndex].userName
			 << " (#" << sigArg->senderIndex + 1 
			 << ") just piped '" << sigArg->message 
			 << "' to " << userInfo[sigArg->receiverIndex] .userName
			 << " (#" << sigArg->receiverIndex + 1 << ") ***" << endl;
		if (sigArg->receiverIndex == statglb_userIndex) {
			string userPipeName = "./user_pipe/" + to_string(sigArg->senderIndex) + "to" + to_string(statglb_userIndex);
			statglb_pipeInFd[sigArg->senderIndex] = open(userPipeName.c_str(), O_RDONLY);
			if (statglb_pipeInFd[sigArg->senderIndex] == -1) {
				cout << "error:" << errno << endl;
			}
		}
		shmdt(userInfo);
	} else if (sigArg->signalMode == PIPERECV) {
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
		cout << "*** " << userInfo[sigArg->senderIndex].userName 
			 << " (#" << sigArg->senderIndex + 1 
			 << ") just received from " << userInfo[sigArg->receiverIndex].userName 
			 << " (#" << sigArg->receiverIndex + 1 
			 << ") by '" << sigArg->message << "' ***" << endl;
		shmdt(userInfo);
	}
	shmdt(sigArg);
	p_sem(statglb_semSigArgID, 1);
}

void login(sockaddr_in clientAddr) {
	/* section: register the user into share memory */
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0); 
	for(int i = 0; i < 30; i++) { /* write user profile to an empty slot */
		if (userInfo[i].used == false) {
			userInfo[i].used = true;
			strcpy(userInfo[i].userName, "(no name)");
			userInfo[i].endpointAddr = clientAddr;
			userInfo[i].processID = getpid();
			statglb_userIndex = i;
			break;
		}
		else if (i == 29) {
			cerr << "unable to create new user." << endl;
			exit(-1);
		}
	}
	/* section: count the existed user */
	int numUser = 0;
	for(int i = 0; i < 30; i++) { 
		if (userInfo[i].used == true) {
			numUser++;
		}
	}
	/* section: release sem with numUser and put args to shm */
	v_sem(statglb_semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = BROADCAST;
	sigArg->signalMode = LOGIN;
	sigArg->senderIndex = statglb_userIndex;
	shmdt(sigArg);
	/* section: signal others to read shm */
	for(int i = 0; i < 30; i++) {
		if(userInfo[i].used == true) {
			if (kill(userInfo[i].processID, SIGUSR1) == -1 && errno == ESRCH) {
				/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
				p_sem(statglb_semSigArgID, 1);
			}
		}
	}
	shmdt(userInfo);
}

void who() {
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
	for(int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			cout << i + 1 << "\t" << userInfo[i].userName 
				 << "\t" << inet_ntoa(userInfo[i].endpointAddr.sin_addr) 
				 << ":" << userInfo[i].endpointAddr.sin_port;
			if (userInfo[i].processID == getpid()) {
				cout << "\t<-me" << endl;
			} else {
				cout << endl;
			}
		}
	}
	shmdt(userInfo);
}

void rename(string name) {
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	for(int i = 0; i < 30; i++) {
		if (strcmp(userInfo[i].userName, name.c_str()) == 0) {
			cout << "*** User '" << name <<"' already exists. ***" << endl;
			return;
		}
	}
	strcpy(userInfo[statglb_userIndex].userName, name.c_str());
	/* section: count the existed user */
	int numUser = 0;
	for (int i = 0; i < 30; i++) { 
		if (userInfo[i].used == true) {
			numUser++;
		}
	}
	/* section: release sem with numUser and put args to shm */
	v_sem(statglb_semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = BROADCAST;
	sigArg->signalMode = RENAME;
	sigArg->senderIndex = statglb_userIndex;
	shmdt(sigArg);
	/* section: signal others to read shm */
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			if (kill(userInfo[i].processID, SIGUSR1) == -1 && errno == ESRCH) {
				/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
				p_sem(statglb_semSigArgID, 1);
			}
		}
	}
	shmdt(userInfo);
}

void yell(vector<string> splitedCmd) {
	string message = splitedCmd[1];
	for (int i = 2; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	int numUser = 0;
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			numUser++;
		}
	}
	v_sem(statglb_semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = BROADCAST;
	sigArg->signalMode = YELL;
	sigArg->senderIndex = statglb_userIndex;
	strcpy(sigArg->message, message.c_str());
	shmdt(sigArg);
	/* section: signal others to read shm */
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			if (kill(userInfo[i].processID, SIGUSR1) == -1 && errno == ESRCH) {
				/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
				p_sem(statglb_semSigArgID, 1);
			}
		}
	}
	shmdt(userInfo);
}

void tell(vector<string> splitedCmd) {
	string message = splitedCmd[2];
	for (int i = 3; i < splitedCmd.size(); i++) {
		message = message + " " + splitedCmd[i];
	}
	v_sem(statglb_semSigArgID, 1);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = UNICAST;
	sigArg->signalMode = TELL;
	sigArg->senderIndex = statglb_userIndex;
	strcpy(sigArg->message, message.c_str());
	shmdt(sigArg);
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	if (stoi(splitedCmd[1]) >= 1 && stoi(splitedCmd[1]) <= 30 && userInfo[stoi(splitedCmd[1]) - 1].used == true) {
		if (kill(userInfo[stoi(splitedCmd[1]) - 1].processID, SIGUSR1) == -1 && errno == ESRCH) {
			/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
			p_sem(statglb_semSigArgID, 1);
		}
	} else {
		cout << "*** Error: user #" << splitedCmd[1] << " does not exist yet. ***" << endl;
		p_sem(statglb_semSigArgID, 1); /* note: undo */
	}
	shmdt(userInfo);
	return;
}

void pipeOut(vector<string> splitedCmd, int receiverIndex) {
	string command = splitedCmd[0];
	for (int i = 1; i < splitedCmd.size(); i++) {
		command = command + " " + splitedCmd[i];
	}
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	/* section: count the existed user */
	int numUser = 0;
	for (int i = 0; i < 30; i++) { 
		if (userInfo[i].used == true) {
			numUser++;
		}
	}
	/* section: release sem with numUser and put args to shm */
	v_sem(statglb_semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = BROADCAST;
	sigArg->signalMode = PIPESEND;
	sigArg->senderIndex = statglb_userIndex;
	sigArg->receiverIndex = receiverIndex;
	strcpy(sigArg->message, command.c_str());
	shmdt(sigArg);
	/* section: signal others to read shm */
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			if (kill(userInfo[i].processID, SIGUSR1) == -1 && errno == ESRCH) {
				/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
				p_sem(statglb_semSigArgID, 1);
			}
		}
	}
	shmdt(userInfo);
}

void pipeIn(vector<string> splitedCmd, int writerIndex) {
	/* concat the command */
	string command = splitedCmd[0];
	for (int i = 1; i < splitedCmd.size(); i++) {
		command = command + " " + splitedCmd[i];
	}
	UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
	/* section: count the existed user */
	int numUser = 0;
	for (int i = 0; i < 30; i++) { 
		if (userInfo[i].used == true) {
			numUser++;
		}
	}
	/* section: release sem with numUser and put args to shm */
	v_sem(statglb_semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	sigArg->castMode = BROADCAST;
	sigArg->signalMode = PIPERECV;
	sigArg->senderIndex = statglb_userIndex;
	sigArg->receiverIndex = writerIndex; /* note: use receiverIndex to store the user who write the pipe */
	strcpy(sigArg->message, command.c_str());
	shmdt(sigArg);
	/* section: signal others to read shm */
	for (int i = 0; i < 30; i++) {
		if (userInfo[i].used == true) {
			if (kill(userInfo[i].processID, SIGUSR1) == -1 && errno == ESRCH) {
				/* note: a terminated process is signaled, should use 1 signal to prevent deadlock */
				p_sem(statglb_semSigArgID, 1);
			}
		}
	}
	shmdt(userInfo);
}

int npshell(pid_t ppid, int shmInfoID, int shmSigArgID, int semSigArgID, sockaddr_in clientAddr) {
	/* note: assign values to static global */
	statglb_semSigArgID = semSigArgID;
	statglb_shmSigArgID = shmSigArgID;
	statglb_shmInfoID = shmInfoID;
	memset(statglb_pipeInFd, 0, sizeof(int) * 30);

	string rawCmd;
	vector<ExecCmd> cmdList;
	vector<vector<int>> createdPipesToEachCmdVec; /* note: register each command's created read Pipe (and another end) */
	vector<vector<int>> assignedEntriesToEachCmdVec; /* note: in and out direction will be assigned and then can be closed by main proccess later */
	clearenv();
	setenv("PATH", "bin:.", 1);
	signal(SIGCHLD, SIGCHLDHandlerShell);
	signal(SIGUSR1, SIGUSR1HandlerShell);
	signal(SIGINT, SIG_IGN);
	/* note: register & signaling process */
	login(clientAddr);
	/* section: start to accept command */
	while (1) {
		int promptCmdCounter = 0;
		string oFile = "";
		int userPipeOut = 0;
		int userPipeIn = 0;
		cout << "% " << flush;
		if (!getline(cin, rawCmd)) {
			exit(0);
		}
		istringstream iss(rawCmd);
		vector<string> splitedCmd(istream_iterator<string>{iss}, 
								  istream_iterator<string>());
		/* section: parse */
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
				userPipeOut = stoi(splitedCmd[i].substr(1));
			}
			else if (splitedCmd[i].length() > 1 && 
					splitedCmd[i].substr(0, 1) == "<" && 
					stoi(splitedCmd[i].substr(1)) >= 1 && 
					stoi(splitedCmd[i].substr(1)) <= 30) { /* note: cross-users pipe */
				userPipeIn = stoi(splitedCmd[i].substr(1));
				//cout << "find read cross user pipe from " << userPipeIn << endl;
				argFlag = 0;
			}
			else { /* note: command */
				if (argFlag) { /* argument */
					cmdList[cmdList.size() - 1].args.push_back(splitedCmd[i]);
				}
				else { /* program */
					argFlag = 1;
					vector<string> tmpVec(1, splitedCmd[i]);
					ExecCmd execCmd = {splitedCmd[i], 0, false, tmpVec};
					cmdList.push_back(execCmd);
					promptCmdCounter++;
				}	
			}
		}
		if (splitedCmd.size() > 0 && splitedCmd[0] == "setenv") {
			setenv(splitedCmd[1].c_str(), splitedCmd[2].c_str(), 1);
			cmdList.erase(cmdList.begin());
			continue;
		}
		if (splitedCmd.size() == 1 && splitedCmd[0] == "who") {
			who();
			cmdList.erase(cmdList.begin());
			continue;
		}
		if (splitedCmd.size() == 2 && splitedCmd[0] == "name") {
			rename(splitedCmd[1]);
			cmdList.erase(cmdList.begin());
			continue;
		}
		if (splitedCmd.size() >= 2 && splitedCmd[0] == "yell") {
			yell(splitedCmd);
			cmdList.erase(cmdList.begin());
			continue;
		}
		if (splitedCmd.size() >= 3 && splitedCmd[0] == "tell") {
			tell(splitedCmd);
			cmdList.erase(cmdList.begin());
			continue;
		}
		if (splitedCmd.size() == 1 && splitedCmd[0] == "exit") {
			exit(0);
		}
		//cout << "test" << endl;
		vector<int> tmpPair(2, 0);
		if (assignedEntriesToEachCmdVec.size() < promptCmdCounter) {
			assignedEntriesToEachCmdVec.resize(promptCmdCounter, tmpPair);
		}
		if (createdPipesToEachCmdVec.size() < promptCmdCounter) {
			createdPipesToEachCmdVec.resize(promptCmdCounter, tmpPair);
		}
		/* section: create new pipe or use existed pipe */
		for (int i = 0; i < promptCmdCounter; i++) {
			/* check whether exists a pipe that should connect to rPipe */
			if (createdPipesToEachCmdVec[i][0] != 0) { /* note: exists a pipe connect to this command */
				assignedEntriesToEachCmdVec[i][0] = createdPipesToEachCmdVec[i][0];
			}
			/* check whether need a pipe to send*/
			if (cmdList[i].pipeToCmd != 0) {
				if (createdPipesToEachCmdVec.size() <= cmdList[i].pipeToCmd + i) {
					createdPipesToEachCmdVec.resize(cmdList[i].pipeToCmd + i + 1, tmpPair);
				}
				if (createdPipesToEachCmdVec[cmdList[i].pipeToCmd + i][0] == 0) { /* note: create new pipe for THAT cmd */
					pipe(&createdPipesToEachCmdVec[cmdList[i].pipeToCmd + i][0]);
				}
				assignedEntriesToEachCmdVec[i][1] = createdPipesToEachCmdVec[cmdList[i].pipeToCmd + i][1];
			}
		}
		if (userPipeIn) {
			UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
			if (userInfo[userPipeIn - 1].used == false) { /* user not exist */
				cout << "*** Error: user #" << userPipeIn << " does not exist yet. ***" << endl;
				userPipeIn = PIPETONULL;
			} else {
				if (statglb_pipeInFd[userPipeIn - 1] == 0) { /* no pipe exist */
					cout << "*** Error: the pipe #" << userPipeIn 
						 << "->#" << statglb_userIndex + 1 << " does not exist yet. ***" << endl;
					userPipeIn = PIPETONULL;
				}
			}
			shmdt(userInfo);
		}
		string userPipeName = "";
		if (userPipeOut) { /* if the last command should output to user pipe */
			UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0);
			if (userInfo[userPipeOut - 1].used == false) { /* user not exist: pipe to null */
				cout << "*** Error: user #" << userPipeOut << " does not exist yet. ***" << endl;
				userPipeOut = PIPETONULL;
			} else {
				userPipeName = "./user_pipe/" + to_string(statglb_userIndex) + "to" + to_string(userPipeOut - 1);
				int result = mkfifo(userPipeName.c_str(), S_IRWXU|S_IRWXG|S_IRWXO);
				if (result == -1 && errno == EEXIST) { /* user pipe already exist: pipe to null */
					cout << "*** Error: the pipe #" << statglb_userIndex + 1 
						 << "->#" << userPipeOut << " already exist. ***" << endl;
					userPipeOut = PIPETONULL;
				} else if (result == -1 && errno != EEXIST) {
					cout << "mkfifo error " << errno << endl;
				}
			}
			shmdt(userInfo);
		}
		vector<int> pidWaitList;
		/* section: fork */
		for (int i = 0; i < promptCmdCounter; i++) {
			pid_t cpid;
			while((cpid = fork()) == -1) {}; /* note: busy waiting if process capacity exhausted */
			if (cpid == 0) { /* note: child */
				//cout << "fork test child" << endl;
				if (assignedEntriesToEachCmdVec[i][0]) { /* note: stdin redirect */
					close(STDIN_FILENO);
					dup2(assignedEntriesToEachCmdVec[i][0], STDIN_FILENO);
				}
				//cout << "fork test child 2" << endl;
				if (assignedEntriesToEachCmdVec[i][1]) { /* note: stdout redirect */
					close(STDOUT_FILENO);
					dup2(assignedEntriesToEachCmdVec[i][1], STDOUT_FILENO);
					if (cmdList[i].errRedir) { /* note: stderr redirect */
						close(STDERR_FILENO);
						dup2(assignedEntriesToEachCmdVec[i][1], STDERR_FILENO);
					}
				}
				//cout << "fork test child 3" << endl;
				if (i == 0) { /* note: the first command */
					if (userPipeIn) { /* note: user attempt to receive pipe from others */
						if (userPipeIn == PIPETONULL) { /* note: invalid attempt */
							int nullFd = open("/dev/null", O_RDONLY);
							close(STDIN_FILENO);
							dup2(nullFd, STDIN_FILENO);
							close(nullFd);
						} else { /* note: valid attempt */
							pipeIn(splitedCmd, userPipeIn - 1);
							close(STDIN_FILENO);
							dup2(statglb_pipeInFd[userPipeIn - 1], STDIN_FILENO);
							close(statglb_pipeInFd[userPipeIn - 1]);
							statglb_pipeInFd[userPipeIn - 1] = 0;
						}
					}
				}
				if (i == promptCmdCounter - 1) { /* note: the last command */
					if (oFile != "") { /* if the last command should output to file */
						int fd = open(oFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
						close(STDOUT_FILENO);
						dup2(fd, STDOUT_FILENO);
					} else if (userPipeOut) { /* note: user attempt to send pipe to others */
						if (userPipeOut == PIPETONULL) { /* note: invalid attempt */
							int nullFd = open("/dev/null", O_WRONLY);
							close(STDOUT_FILENO);
							dup2(nullFd, STDOUT_FILENO);
							close(nullFd);
						} else { /* note: valid attempt */
							pipeOut(splitedCmd, userPipeOut - 1);
							int pipeOutFd = open(userPipeName.c_str(), O_WRONLY);
							close(STDOUT_FILENO);
							dup2(pipeOutFd, STDOUT_FILENO);
							close(pipeOutFd);
						}
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
			}
			else { /* note: parent */
				//cout << "fork test parent" << endl;
				/* section: put commands that no need to hang to the wait list */
				if (i == promptCmdCounter - 1) { /* note: last command */
					if ((userPipeOut == 0 || userPipeOut == PIPETONULL) && cmdList[i].pipeToCmd + i < promptCmdCounter) {
						/* note: no pipeOut and hop pipe */
						pidWaitList.push_back(cpid);
					}
				} else {
					if (cmdList[i].pipeToCmd + i < promptCmdCounter) {
						/* note: no hop pipe */
						pidWaitList.push_back(cpid);
					}
				}
				//cout << "fork test parent 2" << endl;
				/* section: parent close all pipes that do not connect to future commands */
				if (createdPipesToEachCmdVec[i][0]) {
					close(createdPipesToEachCmdVec[i][0]);
				}
				if (createdPipesToEachCmdVec[i][1]) {
					close(createdPipesToEachCmdVec[i][1]);
				}
			}
		}
		//cout << "fork test parent 3" << endl;
		for (int i = 0; i < pidWaitList.size(); i++) {
			int status;
			waitpid(pidWaitList[i], &status, 0);
		}
		if (userPipeIn != 0 && userPipeIn != PIPETONULL) { /* note: parent process should close the pipe that has been received by child process */
			close(statglb_pipeInFd[userPipeIn - 1]);
			statglb_pipeInFd[userPipeIn - 1] = 0;
		}
		cmdList.erase(cmdList.begin(), cmdList.begin() + promptCmdCounter);
		createdPipesToEachCmdVec.erase(createdPipesToEachCmdVec.begin(), createdPipesToEachCmdVec.begin() + promptCmdCounter);
		assignedEntriesToEachCmdVec.erase(assignedEntriesToEachCmdVec.begin(), assignedEntriesToEachCmdVec.begin() + promptCmdCounter);
	}
}