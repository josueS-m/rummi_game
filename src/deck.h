#ifndef DECK_H
#define DECK_H

typedef enum {
	CORAZONES,
	DIAMANTES,
	TREBOLES,
	PICAS
} Suit;

typedef struct {
	int value;
	Suit suit;
	bool is_jocker;
} Card;

void initialize_deck(Card *deck);
void shuffle_deck(Card *deck);

#endif // DECK_H

