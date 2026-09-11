#include "bluffskill/poker/virgo_reference_player.hpp"

#include "reference_player_hand_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

double streetCuriosity(Street street) {
    switch (street) {
    case Street::preflop: return 0.08;
    case Street::flop: return 0.10;
    case Street::turn: return 0.06;
    case Street::river: return 0.02;
    default: return 0.0;
    }
}

} // namespace

VirgoReferencePlayer::VirgoReferencePlayer(std::string name, VirgoReferencePlayerProfile profile)
    : ReferencePlayerController(std::move(name)), profile_(profile) {
    const auto valid = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!valid(profile_.curiosity) || !valid(profile_.hope) || !valid(profile_.empathy) || !valid(profile_.longevity)) {
        throw std::invalid_argument("Virgo reference-player profile values must be between 0 and 1");
    }
}

std::vector<ReferencePlayerParameter> VirgoReferencePlayer::parameters() const {
    return {{"Curiosity", profile_.curiosity}, {"Hope", profile_.hope}, {"Empathy", profile_.empathy}, {"Longevity", profile_.longevity}};
}

ReferenceDecision VirgoReferencePlayer::chooseResponse(const TableView& privateView, std::mt19937_64& random) const {
    if (!privateView.legalActions) throw std::invalid_argument("Virgo reference player was asked to act without legal actions");
    const auto& player = detail::actingPlayer(privateView, name());
    const auto& legal = *privateView.legalActions;
    const auto denominations = privateView.chipDenominations.empty() ? defaultChipDenominations() : privateView.chipDenominations;
    const auto analysis = detail::analyzeHand(privateView, player);
    const auto aggression = detail::observedAggression(privateView, player);
    const auto price = detail::potOdds(privateView, legal);
    const auto threat = std::clamp(analysis.boardThreat + aggression * (0.22 + profile_.empathy * 0.36), 0.0, 1.0);
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    const auto variation = distribution(random) * (0.03 + profile_.hope * 0.04);
    const auto pot = detail::potSize(privateView);
    const auto sizing = [&](double fraction, double stackCap) {
        return detail::sizedCommitment(legal, denominations, player.roundCommitted, pot, fraction, stackCap, player.stack);
    };

    if (legal.check) {
        const auto valueBet = analysis.made >= 0.68 && threat <= 0.66;
        const auto protectedDraw = analysis.draw >= 0.44 && profile_.hope >= 0.55 && threat <= 0.48;
        if (legal.bet && (valueBet || protectedDraw)) {
            const auto fraction = analysis.made >= 0.84 ? 0.62 : protectedDraw ? 0.40 : 0.46;
            const auto stackCap = analysis.made >= 0.90 ? 0.72 : 0.50;
            return {.action = Action::bet, .amount = sizing(fraction, stackCap)};
        }
        return {.action = Action::check};
    }

    const auto raiseScore = analysis.made + analysis.draw * (0.12 + profile_.hope * 0.13) - threat * (0.10 + profile_.empathy * 0.12)
        - price * (0.08 + profile_.longevity * 0.12) + variation;
    if (legal.raise && ((analysis.made >= 0.84 && raiseScore >= 0.70)
        || (analysis.draw >= 0.48 && profile_.hope >= 0.75 && raiseScore >= 0.79))) {
        const auto fraction = analysis.made >= 0.90 ? 0.62 : 0.42;
        return {.action = Action::raise, .amount = sizing(fraction, analysis.made >= 0.90 ? 0.74 : 0.48)};
    }

    const auto continuation = analysis.made + analysis.draw * (0.28 + profile_.hope * 0.24)
        + profile_.curiosity * streetCuriosity(privateView.street) - threat * (0.12 + profile_.empathy * 0.24)
        - profile_.longevity * 0.05 + variation;
    const auto safetyMargin = 0.09 + profile_.longevity * 0.08;
    if (legal.call && continuation >= price + safetyMargin) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Virgo reference player has no legal response");
}

} // namespace bluffskill::poker
