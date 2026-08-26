#include "bluffskill/cards/deck.hpp"

#include <algorithm>

namespace bluffskill::cards {

Deck::Deck() { reset(); }

void Deck::reset() {
    cards_.clear();
    cards_.reserve(52);
    for (auto suit = 0; suit < 4; ++suit) {
        for (auto rank = static_cast<unsigned char>(Rank::two); rank <= static_cast<unsigned char>(Rank::ace); ++rank) {
            cards_.push_back({static_cast<Suit>(suit), static_cast<Rank>(rank)});
        }
    }
}

void Deck::shuffle(std::mt19937_64& random) { std::shuffle(cards_.begin(), cards_.end(), random); }

std::optional<Card> Deck::draw() {
    if (cards_.empty()) return std::nullopt;
    Card card = cards_.back();
    cards_.pop_back();
    return card;
}

std::size_t Deck::remaining() const noexcept { return cards_.size(); }

} // namespace bluffskill::cards
