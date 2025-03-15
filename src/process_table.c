#include "process_table.h"
#include <stdlib.h>

void initialize_process_table(ProcessTable *pt) {
	pt->bank_size = 0;
	pt->current_turn = 0;
	pt->scheduler_mode = 0;
	pt->quantum = 2;
	pthread_mutex_init(&pt->mutex, NULL);
}
