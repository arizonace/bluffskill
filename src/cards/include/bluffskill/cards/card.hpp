#pragma once

#include <compare>
#include <string>

namespace bluffskill::cards {

enum class Suit { clubs, diamonds, hearts, spades };

// Values mirror their poker ordering; this also makes a rank useful to other card games.
enum class Rank : unsigned char {
    two = 2, three, four, five, six, seven, eight, nine, ten,
    jack, queen, king, ace
};

struct Card {
    Suit suit;
    Rank rank;

    auto operator<=>(const Card&) const = default;
};

[[nodiscard]] std::string toString(Suit suit);
[[nodiscard]] std::string toString(Rank rank);
[[nodiscard]] std::string toString(Card card);

} // namespace bluffskill::cards
