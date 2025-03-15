#include "deck.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DECK_SIZE 108

void initialize_deck(Card *deck) {
	int index = 0;
	for (int deck_num = 0; deck_num < 2; deck_num++) {
		for (Suit s = CORAZONES; s <= PICAS; s++) {
			for (int val = 1; val <= 13; val++) {
				deck[index++] = (Card){val, s, false};
			}
		}
	}
	for (int i = 0; i < 4; i++) {
		deck[index++] = (Card){0, CORAZONES, true};
	}
}

void shuffle_deck(Card *deck) {
	for (int i = DECK_SIZE - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		Card temp = deck[i];
		deck[i] = deck[j];
		deck[j] = temp;
	}
}


