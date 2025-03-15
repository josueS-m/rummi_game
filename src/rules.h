#ifndef RULES_H
#define RULES_H

#include "deck.h"

bool is_valid_group(const Card *cards, int length);
bool is_valid_straight(const Card *cards, int length);
bool can_embonar(const PlayedSet *set, Card new_card);

#endif // RULES_H
