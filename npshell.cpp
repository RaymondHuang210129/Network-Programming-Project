#include <cstdlib>
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <iterator>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;

struct ExecCmd {
	string cmd;
	int pipeToCmd;
	bool errRedir;
	vector<string> args;
};

struct PipeCounter {
	vector<int> pipe;
	int index;
};

PipeCounter* isPipeExist(vector<PipeCounter>& pipes, int index) {
	for(int i = 0; i < pipes.size(); i++) {
		if (pipes[i].index == index + 1) {
			return &pipes[i];
		}
	}
	return NULL;
}

void signalHandler(int sigNum) {
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0) {}
	return;
}

int main(int argc, char** argv, char** envp) {
	string rawCmd;
	vector<ExecCmd> cmdList;
	int createdPipesToEachCmd[500000][2]; /* note: register each command's created rPipe (and another end) */
	int assignedEntriesToEachCmd[500000][2]; /* note: in and out direction will be assigned and then can be closed by main proccess later */
	for (int i = 0; i < 500000; i++) {createdPipesToEachCmd[i][0] = 
									   createdPipesToEachCmd[i][1] = 
									   assignedEntriesToEachCmd[i][0] = 
									   assignedEntriesToEachCmd[i][1] = 0;}
	clearenv();
	setenv("PATH", "bin:.", 1);
	signal(SIGCHLD, signalHandler);
	int totalCmdCounter = 0;
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
		for (int i = 0, argFlag = 0; i < splitedCmd.size(); i++) {
			if (splitedCmd[i] == "|") { /* note: regular pipe */
				cmdList[cmdList.size() - 1].pipeToCmd = cmdList.size();
				argFlag = 0;
			}
			else if (splitedCmd[i].length() > 1 && 
					splitedCmd[i].substr(0, 1) == "|" && 
					stoi(splitedCmd[i].substr(1)) >= 1 && 
					stoi(splitedCmd[i].substr(1)) <= 1000) { /* note: hop pipe */
				cmdList[cmdList.size() - 1].pipeToCmd = stoi(splitedCmd[i].substr(1)) + cmdList.size() - 1;
				argFlag = 0;
			}
			else if (splitedCmd[i].length() > 1 && 
					splitedCmd[i].substr(0, 1) == "!" && 
					stoi(splitedCmd[i].substr(1)) >= 1 && 
					stoi(splitedCmd[i].substr(1)) <= 1000) { /* note: err pipe */
				cmdList[cmdList.size() - 1].pipeToCmd = stoi(splitedCmd[i].substr(1)) + cmdList.size() - 1;
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
					totalCmdCounter++;
					promptCmdCounter++;
				}	
			}
		}
		if (splitedCmd[0] == "setenv") {
			setenv(splitedCmd[1].c_str(), splitedCmd[2].c_str(), 1);
			continue;
		}
		/* section: create new pipe or use existed pipe */
		for (int i = totalCmdCounter - promptCmdCounter; i < cmdList.size(); i++) {
			/* check whether exists a pipe that should connect to rPipe */
			if (createdPipesToEachCmd[i][0] != 0) { /* note: exists a pipe connect to this command */
				assignedEntriesToEachCmd[i][0] = createdPipesToEachCmd[i][0];
			}
			/* check whether need a pipe to send*/
			if (cmdList[i].pipeToCmd != 0) {
				if (createdPipesToEachCmd[cmdList[i].pipeToCmd][0] != 0) { /* the pipe for THAT cmd exists */
					assignedEntriesToEachCmd[i][1] = createdPipesToEachCmd[cmdList[i].pipeToCmd][1];
				}
				else { /* note: create new pipe for THAT cmd */
					pipe(createdPipesToEachCmd[cmdList[i].pipeToCmd]);
					assignedEntriesToEachCmd[i][1] = createdPipesToEachCmd[cmdList[i].pipeToCmd][1];
				}
			}
		}
		vector<int> pidWaitList;
		/* section: fork */
		for (int i = totalCmdCounter - promptCmdCounter; i < totalCmdCounter; i++) {
			pid_t cpid;
			while((cpid = fork()) == -1) {}; /* note: busy waiting if process capacity exhausted */
			if (cpid == 0) { /* note: child */
				if (assignedEntriesToEachCmd[i][0]) { /* note: stdin redirect */
					//cout << "process " << i << " redirect stdin with " << assignedEntriesToEachCmd[i][0] << endl;
					close(STDIN_FILENO);
					dup2(assignedEntriesToEachCmd[i][0], STDIN_FILENO);
				}
				if (assignedEntriesToEachCmd[i][1]) { /* note: stdout redirect */
					//cout << "process " << i << " redirect stdout with " << assignedEntriesToEachCmd[i][1] << endl;
					close(STDOUT_FILENO);
					dup2(assignedEntriesToEachCmd[i][1], STDOUT_FILENO);
					if (cmdList[i].errRedir) { /* note: stderr redirect */
						close(STDERR_FILENO);
						dup2(assignedEntriesToEachCmd[i][1], STDERR_FILENO);
					}
				}
				for (int j = 3; j < 100000; j++) {close(j);} /* note: close all unused pipe entries */
				if (i == totalCmdCounter - 1) { /* note: check whether to redirect to file or not */
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
					char* vect[cmdList[i].args.size() + 1];
					for (int j = 0; j < cmdList[i].args.size(); j++) {
						vect[j] = strdup(cmdList[i].args[j].c_str());
					}
					vect[cmdList[i].args.size()] = NULL;
					execvp(vect[0], vect);
					if (errno == 2) {
						cout << "Unknown command: [" << vect[0] <<"]." << endl;
					}
					else {
						cout << "exec error: " << errno << endl;
					}
				}
				exit(0);
			}
			else { /* note: parent */
				if (cmdList[i].pipeToCmd < totalCmdCounter) { /* note: process does not hang till next prompt */
					pidWaitList.push_back(cpid);
				}
			}
		}
		/* note: parent close all pipes that do not connect to future commands */
		for (int i = totalCmdCounter - promptCmdCounter; i < totalCmdCounter; i++) {
			if (createdPipesToEachCmd[i][0]) {
				close(createdPipesToEachCmd[i][0]);
			}
			if (createdPipesToEachCmd[i][1]) {
				close(createdPipesToEachCmd[i][1]);
			}
		}
		for (int i = 0; i < pidWaitList.size(); i++) {
			int status;
			waitpid(pidWaitList[i], &status, 0);
			//cout << i << " process is terminated." << endl;
		}
	}
}


