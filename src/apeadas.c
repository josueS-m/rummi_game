#include "apeadas.h"
#include <stdlib.h>
#include <string.h>

bool is_valid_group(const Card *cards, int length);
bool is_valid_straight(const Card *cards, int length);

void add_played_set(Apeadas *apeadas, const PlayedSet *set) {
	if (apeadas->count >= MAX_PLAYED_SETS || !is_valid_set(set)) return;

	PlayedSet new_set;
	new_set.cards = malloc(set->length * sizeof(Card));
	memcpy(new_set.cards, set->cards, set->length * sizeof(Card));
	new_set.length = set->length;
	new_set.is_group = set->is_group;
	new_set.owner_id = set->owner_id;
	apeadas->sets[apeadas->count++] = new_set;
}

bool is_valid_set(const PlayedSet *set) {
	return set->is_group ? is_valid_group(set->cards, set->length) 
                         : is_valid_straight(set->cards, set->length);
}

