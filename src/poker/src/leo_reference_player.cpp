#include "bluffskill/poker/leo_reference_player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

double rankValue(cards::Rank rank) {
    return (static_cast<double>(rank) - static_cast<double>(cards::Rank::two)) / 12.0;
}

double preflopStrength(const std::vector<cards::Card>& holeCards) {
    if (holeCards.size() != 2) return 0.0;
    const auto first = rankValue(holeCards[0].rank);
    const auto second = rankValue(holeCards[1].rank);
    const auto high = std::max(first, second);
    const auto low = std::min(first, second);
    if (holeCards[0].rank == holeCards[1].rank) return 0.62 + high * 0.38;

    auto strength = 0.07 + high * 0.48 + low * 0.12;
    if (holeCards[0].suit == holeCards[1].suit) strength += 0.08;
    const auto gap = std::abs(static_cast<int>(holeCards[0].rank) - static_cast<int>(holeCards[1].rank));
    if (gap == 1) strength += 0.10;
    else if (gap == 2) strength += 0.05;
    return std::clamp(strength, 0.0, 1.0);
}

double handConfidence(const TableView& view, const TablePlayerView& player) {
    auto confidence = preflopStrength(player.holeCards);
    if (view.communityCards.empty()) return confidence;

    int matchingBoardCards = 0;
    for (const auto hole : player.holeCards) {
        for (const auto board : view.communityCards) {
            if (hole.rank == board.rank) ++matchingBoardCards;
        }
    }
    if (matchingBoardCards > 0) confidence += 0.22 + 0.10 * matchingBoardCards;

    int suitedCards = 0;
    for (const auto hole : player.holeCards) {
        for (const auto board : view.communityCards) {
            if (hole.suit == board.suit) ++suitedCards;
        }
    }
    if (suitedCards >= 3) confidence += 0.08;
    return std::clamp(confidence, 0.0, 1.0);
}

Chips sizedCommitment(const LegalActions& legal, const ChipDenominations& denominations, double confidence, double riskTolerance, double variation) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto fraction = std::clamp(0.18 + confidence * 0.48 + riskTolerance * 0.26 + variation * 0.08, 0.0, 1.0);
    const auto span = static_cast<double>(legal.maximumAmount - legal.minimumAmount);
    const auto candidate = legal.minimumAmount + static_cast<Chips>(std::llround(span * fraction));
    const auto unit = denominations.front();
    const auto roundedUp = ((candidate + unit - 1) / unit) * unit;
    const auto maximumChipValue = (legal.maximumAmount / unit) * unit;
    return std::clamp(std::min(roundedUp, maximumChipValue), legal.minimumAmount, maximumChipValue);
}

} // namespace

LeoReferencePlayer::LeoReferencePlayer(std::string name, LeoReferencePlayerProfile profile)
    : ReferencePlayerController(std::move(name)), profile_(profile) {
    const auto valid = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!valid(profile_.riskTolerance) || !valid(profile_.optimism) || !valid(profile_.variability)) {
        throw std::invalid_argument("Leo reference-player profile values must be between 0 and 1");
    }
}

std::vector<ReferencePlayerParameter> LeoReferencePlayer::parameters() const {
    return {{"Risk Tolerance", profile_.riskTolerance}, {"Optimism", profile_.optimism}, {"Variability", profile_.variability}};
}

ReferenceDecision LeoReferencePlayer::chooseResponse(const TableView& privateView, std::mt19937_64& random) const {
    const auto player = std::ranges::find_if(privateView.players, [this](const TablePlayerView& candidate) {
        return candidate.name == name();
    });
    if (player == privateView.players.end() || !player->acting || !privateView.legalActions) {
        throw std::invalid_argument("Leo reference player was asked to act without a private legal table view");
    }

    const auto& legal = *privateView.legalActions;
    const auto denominations = privateView.chipDenominations.empty() ? defaultChipDenominations() : privateView.chipDenominations;
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    const auto variation = noise(random) * profile_.variability;
    const auto confidence = std::clamp(handConfidence(privateView, *player) + profile_.optimism * 0.18 + variation * 0.16, 0.0, 1.0);
    const auto pressure = legal.callAmount == 0 ? 0.0
        : static_cast<double>(legal.callAmount) / static_cast<double>(std::max<Chips>(1, player->stack + legal.callAmount));

    if (legal.check) {
        const auto betScore = confidence + profile_.riskTolerance * 0.42 + variation * 0.12;
        if (legal.bet && betScore >= 0.94) {
            return {.action = Action::bet, .amount = sizedCommitment(legal, denominations, confidence, profile_.riskTolerance, variation)};
        }
        return {.action = Action::check};
    }

    const auto raiseScore = confidence + profile_.riskTolerance * 0.40 + profile_.optimism * 0.12 - pressure * 0.35 + variation * 0.12;
    if (legal.raise && raiseScore >= 1.08) {
        return {.action = Action::raise, .amount = sizedCommitment(legal, denominations, confidence, profile_.riskTolerance, variation)};
    }

    const auto callScore = confidence + profile_.riskTolerance * 0.34 + profile_.optimism * 0.12 + variation * 0.12;
    if (legal.call && callScore >= 0.47 + pressure * 0.92) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Leo reference player has no legal response");
}

} // namespace bluffskill::poker
