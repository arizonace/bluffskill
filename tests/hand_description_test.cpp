#include <cassert>

// The hand-description formatter is intentionally private to the poker
// engine. Include its implementation here so these examples cover the exact
// server-side formatter without making it a client-facing API.
#include "../src/poker/src/table.cpp"

int main() {
    using bluffskill::cards::Card;
    using bluffskill::cards::Rank;
    using bluffskill::cards::Suit;
    using bluffskill::poker::describeAvailableHand;

    const std::vector<Card> flushBoard{{Suit::spades, Rank::ace}, {Suit::spades, Rank::jack},
        {Suit::spades, Rank::eight}, {Suit::spades, Rank::five}, {Suit::clubs, Rank::two}};
    assert(describeAvailableHand(flushBoard, {{Suit::spades, Rank::queen}, {Suit::spades, Rank::ten}})
        == "Flush(As,Q,10)");

    const std::vector<Card> boardFlush{{Suit::spades, Rank::ace}, {Suit::spades, Rank::king},
        {Suit::spades, Rank::queen}, {Suit::spades, Rank::jack}, {Suit::spades, Rank::nine}};
    assert(describeAvailableHand(boardFlush, {{Suit::spades, Rank::eight}, {Suit::hearts, Rank::two}})
        == "Flush(As)");

    const std::vector<Card> aceHoleFlush{{Suit::spades, Rank::king}, {Suit::spades, Rank::jack},
        {Suit::spades, Rank::eight}, {Suit::spades, Rank::five}, {Suit::clubs, Rank::two}};
    assert(describeAvailableHand(aceHoleFlush, {{Suit::spades, Rank::ace}, {Suit::spades, Rank::queen}})
        == "Flush(As,A,Q)");

    const std::vector<Card> straightBoard{{Suit::clubs, Rank::nine}, {Suit::diamonds, Rank::ten},
        {Suit::hearts, Rank::jack}, {Suit::spades, Rank::queen}, {Suit::clubs, Rank::king}};
    assert(describeAvailableHand(straightBoard, {{Suit::spades, Rank::two}, {Suit::hearts, Rank::three}})
        == "Straight(K)");
}
