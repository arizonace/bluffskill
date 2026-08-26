#include "bluffskill/cards/deck.hpp"

#include <cassert>
#include <random>
#include <set>

int main() {
    bluffskill::cards::Deck deck;
    std::mt19937_64 random{42};
    deck.shuffle(random);
    std::set<bluffskill::cards::Card> cards;
    while (const auto card = deck.draw()) cards.insert(*card);
    assert(cards.size() == 52);
    assert(deck.remaining() == 0);
}
