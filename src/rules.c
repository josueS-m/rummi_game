#include "rules.h"

bool is_valid_group(const Card *cards, int length) {
    if (length < 3 || length > 4) return false;
    int value = cards[0].value;
    for (int i = 1; i < length; i++) {
        if (cards[i].value != value && !cards[i].is_joker) return false;
    }
    return true;
}

bool is_valid_straight(const Card *cards, int length) {
    if (length < 3) return false;
    int expected_value = cards[0].value;
    Suit suit = cards[0].suit;
    int jokers_used = 0;

    for (int i = 1; i < length; i++) {
        if (cards[i].is_joker) {
            jokers_used++;
            expected_value++;
            continue;
        }

        if (cards[i].suit != suit) return false;
        if (cards[i].value != expected_value) {
            while (jokers_used > 0 && expected_value < cards[i].value) {
                jokers_used--;
                expected_value++;
            }
            if (cards[i].value != expected_value) return false;
        }
        expected_value++;
    }
    return true;
}

bool can_embonar(const PlayedSet *set, Card new_card) {
    if (set->is_group) {
        return new_card.value == set->cards[0].value;
    } else {
        return (new_card.value == set->cards[0].value - 1 || 
                new_card.value == set->cards[set->length - 1].value + 1) &&
                new_card.suit == set->cards[0].suit;
    }
}
