
#include "header.hpp"

using namespace std;

static int statglb_semSigArgID;
static int statglb_shmSigArgID;
static int statglb_shmInfoID;

struct ExecCmd {
	string cmd;
	int pipeToCmd;
	bool errRedir;
	vector<string> args;
};

void SIGCHLDHandlerShell(int sigNum) {
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0) {}
	return;
}

void SIGUSR1HandlerShell(int sigNum) { /* note: deal with messages */
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(statglb_shmSigArgID, NULL, 0);
	if (sigArg[0].signalMode == LOGIN) {
		UserInfo* userInfo = (UserInfo*)shmat(statglb_shmInfoID, NULL, 0); 
		cout << "*** User '" << userInfo[sigArg[0].senderIndex].userName 
			 << "' entered from " << inet_ntoa(userInfo[sigArg[0].senderIndex].endpointAddr.sin_addr) 
			 << ":" << userInfo[sigArg[0].senderIndex].endpointAddr.sin_port << ". ***" << endl;
		shmdt(userInfo);
	}
	shmdt(sigArg);
	p_sem(statglb_semSigArgID, 1);
}

int npshell(pid_t ppid, int shmInfoID, int shmSigArgID, int semSigArgID, sockaddr_in clientAddr) {
	/* note: assign values to static global */
	statglb_semSigArgID = semSigArgID;
	statglb_shmSigArgID = shmSigArgID;
	statglb_shmInfoID = shmInfoID;

	string rawCmd;
	vector<ExecCmd> cmdList;
	vector<vector<int>> createdPipesToEachCmdVec; /* note: register each command's created read Pipe (and another end) */
	vector<vector<int>> assignedEntriesToEachCmdVec; /* note: in and out direction will be assigned and then can be closed by main proccess later */
	clearenv();
	setenv("PATH", "bin:.", 1);
	signal(SIGCHLD, SIGCHLDHandlerShell);
	signal(SIGUSR1, SIGUSR1HandlerShell);
	/* section: register the user into share memory */
	int userIndex;
	UserInfo* userInfo = (UserInfo*)shmat(shmInfoID, NULL, 0); 
	for(int i = 0; i < 30; i++) { /* write user profile to an empty slot */
		if (userInfo[i].used == false) {
			userInfo[i].used = true;
			strcpy(userInfo[i].userName, "(no name)");
			userInfo[i].endpointAddr = clientAddr;
			userInfo[i].processID = getpid();
			userIndex = i;
			break;
		}
		else if (i == 29) {
			cerr << "unable to create new user." << endl;
			exit(-1);
		}
	}
	shmdt(userInfo);
	/* note: count the existed user */
	userInfo = (UserInfo*)shmat(shmInfoID, NULL, 0);
	int numUser = 0;
	for(int i = 0; i < 30; i++) { 
		if (userInfo[i].used == true) {
			cout << "used: " << i << endl;
			numUser++;
		}
	}
	/* note: release sem with numUser and put args to shm */
	v_sem(semSigArgID, numUser);
	SIGUSR1Info* sigArg = (SIGUSR1Info*)shmat(shmSigArgID, NULL, 0);
	sigArg[0].castMode = BROADCAST;
	sigArg[0].signalMode = LOGIN;
	sigArg[0].senderIndex = userIndex;
	shmdt(sigArg);
	/* signal others to read shm */
	for(int i = 0; i < 30; i++) {
		if(userInfo[i].used == true) {
			kill(userInfo[i].processID, SIGUSR1);
		}
	}
	shmdt(userInfo);
	/* section: start to accept command */
	while (1) {
		int promptCmdCounter = 0;
		string oFile = "";
		cout << "% " << flush;
		if (!getline(cin, rawCmd)) {exit(0);}
		if (rawCmd == "exit") {exit(0);} /* todo: EOF*/
		istringstream iss(rawCmd);
		vector<string> splitedCmd(istream_iterator<string>{iss}, 
								  istream_iterator<string>());
		/* section: parse */
		if (splitedCmd.size() == 1 && splitedCmd[0] == "exit") {exit(0);}
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
			else { /* note: command */
				if (argFlag) {
					cmdList[cmdList.size() - 1].args.push_back(splitedCmd[i]);
				}
				else {
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
		vector<int> pidWaitList;
		/* section: fork */
		for (int i = 0; i < promptCmdCounter; i++) {
			pid_t cpid;
			while((cpid = fork()) == -1) {}; /* note: busy waiting if process capacity exhausted */
			if (cpid == 0) { /* note: child */
				if (assignedEntriesToEachCmdVec[i][0]) { /* note: stdin redirect */
					close(STDIN_FILENO);
					dup2(assignedEntriesToEachCmdVec[i][0], STDIN_FILENO);
				}
				if (assignedEntriesToEachCmdVec[i][1]) { /* note: stdout redirect */
					close(STDOUT_FILENO);
					dup2(assignedEntriesToEachCmdVec[i][1], STDOUT_FILENO);
					if (cmdList[i].errRedir) { /* note: stderr redirect */
						close(STDERR_FILENO);
						dup2(assignedEntriesToEachCmdVec[i][1], STDERR_FILENO);
					}
				}
				for (int j = 3; j < getdtablesize(); j++) {close(j);} /* note: close all unused pipe entries */
				if (i == promptCmdCounter - 1) { /* note: check whether to redirect to file or not */
					if (oFile != "") {
						int fd = open(oFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
						close(STDOUT_FILENO);
						dup2(fd, STDOUT_FILENO);
					}
				}
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
				if (cmdList[i].pipeToCmd + i < promptCmdCounter) { /* note: process does not hang till next prompt */
					pidWaitList.push_back(cpid);
				}
				/* note: parent close all pipes that do not connect to future commands */
				if (createdPipesToEachCmdVec[i][0]) {
					close(createdPipesToEachCmdVec[i][0]);
				}
				if (createdPipesToEachCmdVec[i][1]) {
					close(createdPipesToEachCmdVec[i][1]);
				}
			}
		}
		for (int i = 0; i < pidWaitList.size(); i++) {
			int status;
			waitpid(pidWaitList[i], &status, 0);
		}
		cmdList.erase(cmdList.begin(), cmdList.begin() + promptCmdCounter);
		createdPipesToEachCmdVec.erase(createdPipesToEachCmdVec.begin(), createdPipesToEachCmdVec.begin() + promptCmdCounter);
		assignedEntriesToEachCmdVec.erase(assignedEntriesToEachCmdVec.begin(), assignedEntriesToEachCmdVec.begin() + promptCmdCounter);
	}
}