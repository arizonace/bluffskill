#include "bluffskill/poker/leo_reference_player.hpp"

#include "reference_player_hand_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

double streetPressureBonus(Street street) {
    switch (street) {
    case Street::preflop: return 0.06;
    case Street::flop: return 0.09;
    case Street::turn: return 0.04;
    case Street::river: return -0.03;
    default: return 0.0;
    }
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
    if (!privateView.legalActions) throw std::invalid_argument("Leo reference player was asked to act without legal actions");
    const auto& player = detail::actingPlayer(privateView, name());
    const auto& legal = *privateView.legalActions;
    const auto denominations = privateView.chipDenominations.empty() ? defaultChipDenominations() : privateView.chipDenominations;
    const auto analysis = detail::analyzeHand(privateView, player);
    const auto aggression = detail::observedAggression(privateView, player);
    const auto price = detail::potOdds(privateView, legal);
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    const auto variation = distribution(random) * profile_.variability;

    const auto handValue = std::clamp(analysis.made + analysis.draw * 0.36 + profile_.optimism * 0.07 + variation * 0.05, 0.0, 1.0);
    const auto pressure = std::clamp(analysis.boardThreat + aggression * 0.45, 0.0, 1.0);
    const auto aggressiveThreshold = 0.67 - profile_.riskTolerance * 0.12 - profile_.optimism * 0.05;
    const auto pot = detail::potSize(privateView);
    const auto sizing = [&](double fraction, double stackCap) {
        return detail::sizedCommitment(legal, denominations, player.roundCommitted, pot, fraction, stackCap, player.stack);
    };

    if (legal.check) {
        const auto continuationBet = handValue + profile_.riskTolerance * 0.10 + streetPressureBonus(privateView.street)
            - pressure * 0.12;
        const auto valueBet = analysis.made >= 0.57;
        const auto semiBluff = analysis.draw >= 0.40 && continuationBet >= aggressiveThreshold - 0.09;
        if (legal.bet && (valueBet || semiBluff || continuationBet >= aggressiveThreshold)) {
            const auto fraction = analysis.made >= 0.80 ? 0.76 : analysis.draw >= 0.40 ? 0.58 : 0.48;
            const auto stackCap = analysis.made >= 0.88 ? 0.85 : 0.62;
            return {.action = Action::bet, .amount = sizing(fraction, stackCap)};
        }
        return {.action = Action::check};
    }

    const auto raiseScore = handValue + profile_.riskTolerance * 0.13 - price * 0.20 - pressure * 0.08;
    const auto strongValue = analysis.made >= 0.77;
    const auto strongDraw = analysis.draw >= 0.44 && raiseScore >= aggressiveThreshold + 0.02;
    if (legal.raise && (strongValue || strongDraw) && raiseScore >= aggressiveThreshold) {
        const auto fraction = strongValue ? 0.72 : 0.55;
        const auto stackCap = analysis.made >= 0.90 ? 0.88 : 0.60;
        return {.action = Action::raise, .amount = sizing(fraction, stackCap)};
    }

    const auto callScore = analysis.made + analysis.draw * 0.49 + profile_.riskTolerance * 0.08 + profile_.optimism * 0.05
        - pressure * 0.16 + variation * 0.04;
    if (legal.call && callScore >= price + 0.05) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Leo reference player has no legal response");
}

} // namespace bluffskill::poker
