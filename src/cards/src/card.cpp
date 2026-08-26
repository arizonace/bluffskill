#include "bluffskill/cards/card.hpp"

#include <array>
#include <stdexcept>

namespace bluffskill::cards {

std::string toString(Suit suit) {
    constexpr std::array names{"clubs", "diamonds", "hearts", "spades"};
    const auto index = static_cast<std::size_t>(suit);
    if (index >= names.size()) throw std::invalid_argument("invalid card suit");
    return names[index];
}

std::string toString(Rank rank) {
    constexpr std::array names{"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    const auto value = static_cast<unsigned char>(rank);
    if (value < static_cast<unsigned char>(Rank::two) || value > static_cast<unsigned char>(Rank::ace)) {
        throw std::invalid_argument("invalid card rank");
    }
    return names[value - static_cast<unsigned char>(Rank::two)];
}

std::string toString(Card card) {
    return toString(card.rank) + " of " + toString(card.suit);
}

} // namespace bluffskill::cards
