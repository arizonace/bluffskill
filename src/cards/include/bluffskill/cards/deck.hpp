#pragma once

#include "bluffskill/cards/card.hpp"

#include <optional>
#include <random>
#include <vector>

namespace bluffskill::cards {

class Deck {
public:
    Deck();

    void reset();
    void shuffle(std::mt19937_64& random);
    [[nodiscard]] std::optional<Card> draw();
    [[nodiscard]] std::size_t remaining() const noexcept;

private:
    std::vector<Card> cards_;
};

} // namespace bluffskill::cards
