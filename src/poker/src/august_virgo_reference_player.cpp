#include "bluffskill/poker/august_virgo_reference_player.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

constexpr int rankCount = 15;

double rankValue(cards::Rank rank) {
    return (static_cast<double>(rank) - static_cast<double>(cards::Rank::two)) / 12.0;
}

const TablePlayerView* actingPlayer(const TableView& view, std::string_view name) {
    const auto player = std::ranges::find_if(view.players, [name](const TablePlayerView& candidate) {
        return candidate.name == name;
    });
    return player == view.players.end() ? nullptr : &*player;
}

double madeHandStrength(const TableView& view, const TablePlayerView& player) {
    if (player.holeCards.size() != 2) return 0.0;
    const auto first = rankValue(player.holeCards[0].rank);
    const auto second = rankValue(player.holeCards[1].rank);
    auto strength = player.holeCards[0].rank == player.holeCards[1].rank
        ? 0.46 + std::max(first, second) * 0.38
        : 0.06 + std::max(first, second) * 0.34 + std::min(first, second) * 0.10;
    if (player.holeCards[0].suit == player.holeCards[1].suit) strength += 0.05;

    std::array<int, rankCount> ranks{};
    for (const auto card : view.communityCards) ++ranks[static_cast<int>(card.rank)];
    for (const auto card : player.holeCards) ++ranks[static_cast<int>(card.rank)];
    const auto bestCount = *std::ranges::max_element(ranks);
    if (bestCount == 4) strength = std::max(strength, 0.97);
    else if (bestCount == 3) strength = std::max(strength, 0.78);
    else if (bestCount == 2) strength = std::max(strength, 0.48);
    return std::clamp(strength, 0.0, 1.0);
}

double drawPotential(const TableView& view, const TablePlayerView& player) {
    if (view.communityCards.empty() || player.holeCards.size() != 2) return 0.0;
    std::array<int, 4> suits{};
    std::array<int, rankCount> ranks{};
    for (const auto card : view.communityCards) {
        ++suits[static_cast<int>(card.suit)];
        ++ranks[static_cast<int>(card.rank)];
    }
    for (const auto card : player.holeCards) {
        ++suits[static_cast<int>(card.suit)];
        ++ranks[static_cast<int>(card.rank)];
    }

    double potential = 0.0;
    const auto mostSuited = *std::ranges::max_element(suits);
    if (mostSuited >= 4) potential = 0.62;
    else if (mostSuited == 3) potential = 0.28;

    int bestStraightWindow = 0;
    for (int high = 14; high >= 5; --high) {
        int seen = 0;
        for (int rank = high; rank > high - 5; --rank) seen += ranks[rank] > 0;
        bestStraightWindow = std::max(bestStraightWindow, seen);
    }
    if (ranks[14] && ranks[2] && ranks[3] && ranks[4] && ranks[5]) bestStraightWindow = 5;
    if (bestStraightWindow >= 4) potential = std::max(potential, 0.56);
    else if (bestStraightWindow == 3) potential = std::max(potential, 0.22);
    return potential;
}

double boardThreatProbability(const TableView& view, const TablePlayerView& player) {
    const auto activeOpponents = std::count_if(view.players.begin(), view.players.end(), [&player](const TablePlayerView& candidate) {
        return candidate.name != player.name && !candidate.folded && (candidate.stack > 0 || candidate.committed > 0);
    });
    const auto opponentFactor = std::min(1.0, static_cast<double>(activeOpponents) / 4.0);
    if (view.communityCards.empty()) return 0.10 + opponentFactor * 0.18;

    std::array<int, 4> suits{};
    std::array<int, rankCount> ranks{};
    for (const auto card : view.communityCards) {
        ++suits[static_cast<int>(card.suit)];
        ++ranks[static_cast<int>(card.rank)];
    }
    const auto mostSuited = *std::ranges::max_element(suits);
    const auto pairedBoard = *std::ranges::max_element(ranks) >= 2;
    int bestStraightWindow = 0;
    for (int high = 14; high >= 5; --high) {
        int seen = 0;
        for (int rank = high; rank > high - 5; --rank) seen += ranks[rank] > 0;
        bestStraightWindow = std::max(bestStraightWindow, seen);
    }

    auto probability = 0.14 + opponentFactor * 0.22;
    if (pairedBoard) probability += 0.10;
    if (mostSuited >= 4) probability += 0.30;
    else if (mostSuited == 3) probability += 0.15;
    if (bestStraightWindow >= 4) probability += 0.26;
    else if (bestStraightWindow == 3) probability += 0.10;
    return std::clamp(probability, 0.0, 1.0);
}

double observedAggression(const TableView& view, const TablePlayerView& player) {
    double aggression = 0.0;
    for (const auto& action : view.actionHistory) {
        if (action.player == player.name || action.street != view.street) continue;
        switch (action.action) {
        case Action::bet:
        case Action::raise:
            aggression += 0.16 + std::min(0.20, static_cast<double>(action.amount) / std::max<Chips>(1, action.stackAfter + action.amount));
            break;
        case Action::call: aggression += 0.04; break;
        default: break;
        }
    }
    return std::clamp(aggression, 0.0, 1.0);
}

double streetCuriosity(Street street) {
    switch (street) {
    case Street::preflop: return 0.62;
    case Street::flop: return 0.82;
    case Street::turn: return 0.68;
    case Street::river: return 0.36;
    default: return 0.0;
    }
}

Chips sizedCommitment(const LegalActions& legal, const ChipDenominations& denominations, double confidence, double longevity) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto fraction = std::clamp(0.16 + confidence * 0.48 + (1.0 - longevity) * 0.18, 0.0, 1.0);
    const auto candidate = legal.minimumAmount + static_cast<Chips>(std::llround(
        static_cast<double>(legal.maximumAmount - legal.minimumAmount) * fraction));
    const auto unit = denominations.front();
    const auto roundedUp = ((candidate + unit - 1) / unit) * unit;
    const auto maximumChipValue = (legal.maximumAmount / unit) * unit;
    return std::clamp(std::min(roundedUp, maximumChipValue), legal.minimumAmount, maximumChipValue);
}

} // namespace

AugustVirgoReferencePlayer::AugustVirgoReferencePlayer(std::string name, AugustVirgoReferencePlayerProfile profile)
    : ReferencePlayerController(std::move(name)), profile_(profile) {
    const auto valid = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!valid(profile_.curiosity) || !valid(profile_.hope) || !valid(profile_.empathy) || !valid(profile_.longevity)) {
        throw std::invalid_argument("Virgo reference-player profile values must be between 0 and 1");
    }
}

std::vector<ReferencePlayerParameter> AugustVirgoReferencePlayer::parameters() const {
    return {{"Curiosity", profile_.curiosity}, {"Hope", profile_.hope}, {"Empathy", profile_.empathy}, {"Longevity", profile_.longevity}};
}

ReferenceDecision AugustVirgoReferencePlayer::chooseResponse(const TableView& privateView, std::mt19937_64&) const {
    const auto* player = actingPlayer(privateView, name());
    if (player == nullptr || !player->acting || !privateView.legalActions) {
        throw std::invalid_argument("Virgo reference player was asked to act without a private legal table view");
    }

    const auto& legal = *privateView.legalActions;
    const auto denominations = privateView.chipDenominations.empty() ? defaultChipDenominations() : privateView.chipDenominations;
    const auto pressure = legal.callAmount == 0 ? 0.0
        : static_cast<double>(legal.callAmount) / std::max<Chips>(1, player->stack + legal.callAmount);
    const auto made = madeHandStrength(privateView, *player);
    const auto draw = drawPotential(privateView, *player);
    const auto opponentThreat = std::clamp(boardThreatProbability(privateView, *player)
        + observedAggression(privateView, *player) * profile_.empathy, 0.0, 1.0);
    const auto continuation = made * 0.62 + draw * (0.16 + profile_.hope * 0.34)
        + profile_.curiosity * streetCuriosity(privateView.street) * 0.22
        - opponentThreat * (0.16 + profile_.empathy * 0.34)
        - pressure * (0.18 + profile_.longevity * 0.48);

    if (legal.check) {
        const auto betScore = made + draw * profile_.hope * 0.30 - opponentThreat * (0.20 + profile_.empathy * 0.18)
            - profile_.longevity * 0.18;
        if (legal.bet && betScore >= 0.86) {
            return {.action = Action::bet, .amount = sizedCommitment(legal, denominations, made + draw * profile_.hope, profile_.longevity)};
        }
        return {.action = Action::check};
    }

    const auto raiseScore = made * 1.05 + draw * profile_.hope * 0.34 - opponentThreat * (0.18 + profile_.empathy * 0.22)
        - pressure * (0.10 + profile_.longevity * 0.26);
    if (legal.raise && raiseScore >= 0.90) {
        return {.action = Action::raise, .amount = sizedCommitment(legal, denominations, made + draw * profile_.hope, profile_.longevity)};
    }
    if (legal.call && continuation >= 0.26) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Virgo reference player has no legal response");
}

} // namespace bluffskill::poker
