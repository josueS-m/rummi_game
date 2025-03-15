#include "pcb.h"
#include <stdlib.h>

void initialize_pcb(PCB *pcb, int pid) {
	pcb->pid = pid;
	pcb->state = READY;
	pcb->hand_capacity = 20;
	pcb->hand = malloc(pcb->hand_capacity * sizeof(Card));
	pcb->hand_size = 0;
	pcb->score = 0;
	pcb->joker_count = 0;
	pcb->skipped_turns = 0;
}

void free_pcb(PCB *pcb) {
	free(pcb->hand);
	pcb->hand = NULL;
	pcb->hand_size = 0;
	pcb->hand_capacity = 0;
}
