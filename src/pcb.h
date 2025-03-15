#ifndef PCB_H
#define PCB_H

#include "deck.h"

typedef enum {
	READY,
	RUNNING,
	BLOCKED
} ProcessState;

typedef struct {
	int pid;			//ID del jugador
	pthread_t thread_id;		//Hilo asoociado
	ProcessState state; 		//Estado actual
	Card *hand;			//Cartas en mano (memoria dinamica)
	int hand_size;			//Numero de cartas en mano
	int hand_capacity; 		//Capacidad actual del array
	int score;			//Puntaje
	int time_quantum;		//Tiempo asignado por el planificador
	int time_remaining;		//Tiempo restante en turno
	int blocked_time;		//Tiempo en cola de bloqueados
	int joker_count;		//Numero de comodines
	Card jokers[2];			//Comodines (maximo 2)
	int skipped_turns;		//Turnos perdidos
	int cards_played;		//Cartas jugadas en mesa
} PCB;

void initialize_pcb(PCB *pcb, int pid);
void free_pcb(PCB *pcb);

#endif // PCB_H

