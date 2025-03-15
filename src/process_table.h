#ifndef PROCESS_TABLE_H
#define PROCESS_TABLE_H

#include "pcb.h"
#include "deck.h"

#define MAX_PLAYERS 4
#define MAX_BANK_CARDS 50

typedef struct {
	PCB players[MAX_PLAYERS]; 		//Jugadores
	Card bank[MAX_BANK_CARDS];		//Banca
	int bank_size;				//Cartas en banca
	int current_turn;			//Indice del jugador
	int scheduler_mode;			//0: FCFS, 1: Round Robin
	int quantum;				//Quantum para Round Robin
	pthread_mutex_t	mutex;			//Mutex para sincronización
} ProcessTable;

void initialize_process_table(ProcessTable *pt);

#endif // PROCESS_TABLE_H
