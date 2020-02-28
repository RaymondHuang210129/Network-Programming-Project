
#include "header.hpp"

using namespace std;

int init_sem(key_t key, int semVal) {
	//cout << "init_sem" << endl;
	int semID;
	union semun semUnion;
	semID = semget(key, 1, 0644|IPC_CREAT);
	if (semVal >= 0) {
		semUnion.val = semVal;
		if (semctl(semID, 0, SETVAL, semUnion) == -1) {
			return -1;
		}
	}
	return semID;
}

int del_sem(int semID) {
	//cout << "del_sem" << endl;
	union semun semUnion;
	if (semctl(semID, 0, IPC_RMID, semUnion) == -1) {
		return -1;
	} else {
		//cout << "del_sem finish" << endl;
		return 0;
	}
}

int v_sem(int semID, int value) { /* release */
	//cout << "v_sem" << endl;
	struct sembuf semBuffer;
	semBuffer.sem_num = 0;
	semBuffer.sem_op = 0;
	semBuffer.sem_flg = 0; //SEM_UNDO
	if (semop(semID, &semBuffer, 1) == -1) {
		cerr << "sem v0 failed." << endl;
		return -1;
	}
	//cout << "v_sem +" << value << endl;
	struct sembuf semBuffer2;
	semBuffer2.sem_num = 0;
	semBuffer2.sem_op = value;
	semBuffer2.sem_flg = 0; //SEM_UNDO
	if (semop(semID, &semBuffer2, 1) == -1) {
		cerr << "sem v1 failed." << endl;
		return -1;
	}
	//cout << "v_sem finished" << endl;
	return 0;
}

int p_sem(int semID, int value) { /* use */
	//cout << "p_sem -" << value << endl;
	struct sembuf semBuffer;
	semBuffer.sem_num = 0;
	semBuffer.sem_op = -value;
	semBuffer.sem_flg = 0; //SEM_UNDO
	if (semop(semID, &semBuffer, 1) == -1) {
		cerr << "sem p failed." << endl;
		return -1;
	}
	//	cout << "p_sem finished" << endl;
	return 0;
} 