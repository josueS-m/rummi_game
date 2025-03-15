#ifndef APEADAS_H
#define APEADAS_H

#include "deck.h"

#define MAX_PLAYED_SETS 20
#define MAX_CARDS_IN_SET 10

typedef struct {
	Card *cards;
	int length;
	bool is_group;
	int owner_id;
} PlayedSet;

typedef struct {
	PlayedSet sets[MAX_PLAYED_SETS];
	int count;
} Apeadas;

void add_played_set(Apeadas *apeadas, const PlayedSet *set);
bool is_valid_set(const PlayedSet *set);

#endif // APEADAS_H
