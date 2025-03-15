#include "scheduler.h"

void fcfs_scheduler(ProcessTable *pt) {
	pthread_mutex_lock(&pt->mutex);
	for (int i = 0; i < MAX_PLAYERS; i++) {
		int idx = (pt->current_turn + i) % MAX_PLAYERS;
		if (pt->players[idx].state == READY) {
           	 	pt->current_turn = idx;
            		break;
        	}
	}
	pthread_mutex_unlock(&pt->mutex);	
}

void round_robin_scheduler(ProcessTable *pt) {
	pthread_mutex_lock(&pt->mutex);
	int start = pt->current_turn;
	do {
		pt->current_turn = (pt->current_turn + 1) % MAX_PLAYERS;
	} while	(pt->players[pt->current_turn].state != READY && pt->current_turn != start);
	pthread_mutex_unlock(&pt->mutex);
}
