#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process_table.h"

void fcfs_scheduler(ProcessTable *pt);
void round_robin_scheduler(ProcessTable *pt);

#endif // SCHEDULER_H
