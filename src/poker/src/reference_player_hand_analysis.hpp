#pragma once

#include "bluffskill/poker/reference_player.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace bluffskill::poker::detail {

struct HandAnalysis {
    double preflop{0.0};
    double made{0.0};
    double draw{0.0};
    double boardThreat{0.0};
    std::size_t opponents{0};
};

inline double rankValue(cards::Rank rank) {
    return (static_cast<double>(rank) - static_cast<double>(cards::Rank::two)) / 12.0;
}

inline double preflopStrength(const std::vector<cards::Card>& holeCards) {
    if (holeCards.size() != 2) return 0.0;
    const auto first = rankValue(holeCards[0].rank);
    const auto second = rankValue(holeCards[1].rank);
    const auto high = std::max(first, second);
    const auto low = std::min(first, second);
    if (holeCards[0].rank == holeCards[1].rank) return 0.58 + high * 0.42;

    auto strength = 0.05 + high * 0.50 + low * 0.14;
    if (holeCards[0].suit == holeCards[1].suit) strength += 0.07;
    const auto gap = std::abs(static_cast<int>(holeCards[0].rank) - static_cast<int>(holeCards[1].rank));
    if (gap == 1) strength += 0.09;
    else if (gap == 2) strength += 0.04;
    return std::clamp(strength, 0.0, 1.0);
}

inline int straightHigh(const std::array<int, 15>& ranks) {
    for (int high = 14; high >= 5; --high) {
        bool complete = true;
        for (int rank = high; rank > high - 5; --rank) complete = complete && ranks[rank] > 0;
        if (complete) return high;
    }
    return ranks[14] && ranks[2] && ranks[3] && ranks[4] && ranks[5] ? 5 : 0;
}

inline int bestStraightWindow(const std::array<int, 15>& ranks) {
    int best = 0;
    for (int high = 14; high >= 5; --high) {
        int seen = 0;
        for (int rank = high; rank > high - 5; --rank) seen += ranks[rank] > 0;
        best = std::max(best, seen);
    }
    if (ranks[14] && ranks[2] && ranks[3] && ranks[4] && ranks[5]) best = 5;
    return best;
}

// A compact, showdown-aware estimate.  It deliberately values a board-only
// hand less than a hand improved by the player's private cards.
inline double madeHandScore(const std::vector<cards::Card>& cards) {
    if (cards.empty()) return 0.0;
    std::array<int, 15> ranks{};
    std::array<int, 4> suits{};
    for (const auto card : cards) {
        ++ranks[static_cast<int>(card.rank)];
        ++suits[static_cast<int>(card.suit)];
    }

    const auto highCard = [&] {
        for (int rank = 14; rank >= 2; --rank) if (ranks[rank]) return rank;
        return 2;
    }();
    const auto flushSuit = std::ranges::find_if(suits, [](int count) { return count >= 5; });
    const auto straight = straightHigh(ranks);
    if (flushSuit != suits.end()) {
        std::array<int, 15> flushRanks{};
        const auto wantedSuit = static_cast<std::size_t>(std::distance(suits.begin(), flushSuit));
        for (const auto card : cards) if (static_cast<std::size_t>(card.suit) == wantedSuit) ++flushRanks[static_cast<int>(card.rank)];
        if (const auto straightFlush = straightHigh(flushRanks); straightFlush > 0) return 0.985 + rankValue(static_cast<cards::Rank>(straightFlush)) * 0.015;
    }

    int pairs = 0;
    int triples = 0;
    int quads = 0;
    int bestPair = 2;
    int bestTriple = 2;
    for (int rank = 14; rank >= 2; --rank) {
        if (ranks[rank] == 4) { ++quads; bestTriple = std::max(bestTriple, rank); }
        else if (ranks[rank] == 3) { ++triples; bestTriple = std::max(bestTriple, rank); }
        else if (ranks[rank] == 2) { ++pairs; bestPair = std::max(bestPair, rank); }
    }
    if (quads) return 0.95 + rankValue(static_cast<cards::Rank>(bestTriple)) * 0.03;
    if (triples && (pairs || triples > 1)) return 0.88 + rankValue(static_cast<cards::Rank>(bestTriple)) * 0.04;
    if (flushSuit != suits.end()) return 0.80 + rankValue(static_cast<cards::Rank>(highCard)) * 0.06;
    if (straight) return 0.74 + rankValue(static_cast<cards::Rank>(straight)) * 0.05;
    if (triples) return 0.64 + rankValue(static_cast<cards::Rank>(bestTriple)) * 0.07;
    if (pairs >= 2) return 0.52 + rankValue(static_cast<cards::Rank>(bestPair)) * 0.08;
    if (pairs == 1) return 0.36 + rankValue(static_cast<cards::Rank>(bestPair)) * 0.10;
    return 0.08 + rankValue(static_cast<cards::Rank>(highCard)) * 0.16;
}

inline double drawScore(const TableView& view, const TablePlayerView& player) {
    if (view.communityCards.size() < 3 || view.communityCards.size() == 5 || player.holeCards.size() != 2) return 0.0;
    std::array<int, 15> ranks{};
    std::array<int, 4> suits{};
    std::array<bool, 4> privateSuit{};
    for (const auto card : view.communityCards) {
        ++ranks[static_cast<int>(card.rank)];
        ++suits[static_cast<int>(card.suit)];
    }
    for (const auto card : player.holeCards) {
        ++ranks[static_cast<int>(card.rank)];
        ++suits[static_cast<int>(card.suit)];
        privateSuit[static_cast<int>(card.suit)] = true;
    }

    double score = 0.0;
    for (std::size_t suit = 0; suit < suits.size(); ++suit) {
        if (!privateSuit[suit]) continue;
        if (suits[suit] == 4) score = std::max(score, 0.48);
        else if (suits[suit] == 3) score = std::max(score, 0.22);
    }
    for (int high = 14; high >= 5; --high) {
        int seen = 0;
        bool privateCardInWindow = false;
        for (int rank = high; rank > high - 5; --rank) {
            seen += ranks[rank] > 0;
            privateCardInWindow = privateCardInWindow || std::ranges::any_of(player.holeCards, [rank](const auto& card) {
                return static_cast<int>(card.rank) == rank;
            });
        }
        if (!privateCardInWindow) continue;
        if (seen == 4) score = std::max(score, 0.44);
        else if (seen == 3) score = std::max(score, 0.16);
    }
    return score;
}

inline double boardThreat(const TableView& view, const TablePlayerView& player) {
    std::array<int, 15> ranks{};
    std::array<int, 4> suits{};
    for (const auto card : view.communityCards) {
        ++ranks[static_cast<int>(card.rank)];
        ++suits[static_cast<int>(card.suit)];
    }
    const auto opponents = std::count_if(view.players.begin(), view.players.end(), [&player](const auto& candidate) {
        return candidate.name != player.name && !candidate.folded && (candidate.stack > 0 || candidate.committed > 0);
    });
    auto threat = std::min(0.24, static_cast<double>(opponents) * 0.06);
    if (!view.communityCards.empty()) {
        if (*std::ranges::max_element(ranks) >= 2) threat += 0.08;
        const auto mostSuited = *std::ranges::max_element(suits);
        if (mostSuited >= 4) threat += 0.22;
        else if (mostSuited == 3) threat += 0.10;
        const auto straightWindow = bestStraightWindow(ranks);
        if (straightWindow >= 4) threat += 0.16;
        else if (straightWindow == 3) threat += 0.06;
    }
    return std::clamp(threat, 0.0, 0.75);
}

inline double observedAggression(const TableView& view, const TablePlayerView& player) {
    double aggression = 0.0;
    for (const auto& action : view.actionHistory) {
        if (action.player == player.name || action.street != view.street) continue;
        if (action.action != Action::bet && action.action != Action::raise) continue;
        const auto stackBefore = std::max<Chips>(1, action.stackAfter + action.amount);
        aggression += 0.12 + std::min(0.24, static_cast<double>(action.amount) / static_cast<double>(stackBefore));
    }
    return std::clamp(aggression, 0.0, 1.0);
}

inline HandAnalysis analyzeHand(const TableView& view, const TablePlayerView& player) {
    std::vector<cards::Card> allCards = view.communityCards;
    allCards.insert(allCards.end(), player.holeCards.begin(), player.holeCards.end());
    const auto preflop = preflopStrength(player.holeCards);
    const auto allScore = madeHandScore(allCards);
    const auto boardScore = madeHandScore(view.communityCards);
    const auto privateImprovement = std::max(0.0, allScore - boardScore);
    const auto made = view.communityCards.empty() ? preflop
        : std::clamp(allScore * 0.58 + privateImprovement * 0.58 + preflop * 0.18, 0.0, 1.0);
    const auto opponents = static_cast<std::size_t>(std::count_if(view.players.begin(), view.players.end(), [&player](const auto& candidate) {
        return candidate.name != player.name && !candidate.folded && (candidate.stack > 0 || candidate.committed > 0);
    }));
    return {.preflop = preflop, .made = made, .draw = drawScore(view, player), .boardThreat = boardThreat(view, player), .opponents = opponents};
}

inline Chips potSize(const TableView& view) {
    return std::accumulate(view.pots.begin(), view.pots.end(), Chips{0}, [](Chips total, const PotView& pot) {
        return total + pot.amount;
    });
}

inline Chips sizedCommitment(const LegalActions& legal, const ChipDenominations& denominations, Chips currentCommitment,
    Chips pot, double potFraction, double maximumStackFraction, Chips stack) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto unit = denominations.empty() ? Chips{1} : denominations.front();
    const auto desired = currentCommitment + static_cast<Chips>(std::llround(std::max<Chips>(pot, unit) * potFraction));
    const auto conservativeCap = currentCommitment + static_cast<Chips>(std::floor(stack * maximumStackFraction / unit)) * unit;
    const auto capped = std::min({desired, legal.maximumAmount, std::max(legal.minimumAmount, conservativeCap)});
    const auto roundedUp = ((capped + unit - 1) / unit) * unit;
    return std::clamp(std::min(roundedUp, legal.maximumAmount), legal.minimumAmount, legal.maximumAmount);
}

inline double potOdds(const TableView& view, const LegalActions& legal) {
    const auto pot = potSize(view);
    return legal.callAmount == 0 ? 0.0
        : static_cast<double>(legal.callAmount) / static_cast<double>(std::max<Chips>(1, pot + legal.callAmount));
}

inline const TablePlayerView& actingPlayer(const TableView& view, std::string_view name) {
    const auto found = std::ranges::find_if(view.players, [name](const auto& candidate) { return candidate.name == name; });
    if (found == view.players.end() || !found->acting) throw std::invalid_argument("reference player was asked to act without a private legal table view");
    return *found;
}

} // namespace bluffskill::poker::detail
