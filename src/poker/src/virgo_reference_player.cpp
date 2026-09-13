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

bool isHeadsUp(const detail::HandAnalysis& analysis) {
    return analysis.opponents == 1;
}

std::size_t preflopRaiseCount(const TableView& view) {
    return static_cast<std::size_t>(std::count_if(view.actionHistory.begin(), view.actionHistory.end(), [](const auto& action) {
        return action.street == Street::preflop && action.action == Action::raise;
    }));
}

Chips roundedCommitment(Chips desired, const LegalActions& legal, const ChipDenominations& denominations) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto unit = denominations.empty() ? Chips{1} : denominations.front();
    const auto rounded = ((std::max(desired, legal.minimumAmount) + unit - 1) / unit) * unit;
    return std::clamp(rounded, legal.minimumAmount, legal.maximumAmount);
}

ReferenceDecision headsUpPreflopDecision(const TableView& view, const TablePlayerView& player, const LegalActions& legal,
    const ChipDenominations& denominations, const detail::HandAnalysis& analysis, const VirgoReferencePlayerProfile& profile,
    double variation) {
    const auto raises = preflopRaiseCount(view);
    const auto strength = std::clamp(analysis.preflop + profile.curiosity * 0.03 + profile.hope * 0.03 + variation, 0.0, 1.0);
    const auto foldOrCheck = [&] {
        if (legal.check) return ReferenceDecision{.action = Action::check};
        if (legal.fold) return ReferenceDecision{.action = Action::fold};
        if (legal.call) return ReferenceDecision{.action = Action::call};
        throw std::logic_error("Virgo reference player has no legal heads-up preflop response");
    };
    const auto raise = [&] {
        const auto bigBlind = std::max<Chips>(1, view.bigBlind);
        const auto desired = raises == 0
            ? static_cast<Chips>(std::llround(2.2 * bigBlind))
            : static_cast<Chips>(std::llround(view.currentBet * 3.1));
        return ReferenceDecision{.action = Action::raise, .amount = roundedCommitment(desired, legal, denominations)};
    };

    if (raises == 0) {
        // Virgo leads selectively heads-up instead of completing or calling a
        // broad range. Its modest opening size preserves the archetype's
        // preference for controlled pots.
        const auto openThreshold = 0.57 - profile.curiosity * 0.02 - profile.hope * 0.02;
        if (legal.raise && strength >= openThreshold) return raise();
        if (legal.call && strength >= 0.50) return {.action = Action::call};
        return foldOrCheck();
    }

    if (raises == 1) {
        const auto threeBetThreshold = 0.82 - profile.hope * 0.03;
        if (legal.raise && strength >= threeBetThreshold) return raise();
        // The price matters, but does not turn low-quality suited hands into
        // automatic flats against an assertive heads-up opponent.
        const auto callThreshold = 0.61 + std::min(0.06, detail::potOdds(view, legal) * 0.12) - profile.curiosity * 0.02;
        if (legal.call && strength >= callThreshold) return {.action = Action::call};
        return foldOrCheck();
    }

    if (legal.raise && strength >= 0.91) return raise();
    if (legal.call && strength >= 0.72) return {.action = Action::call};
    return foldOrCheck();
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
    const auto headsUp = isHeadsUp(analysis);
    const auto sizing = [&](double fraction, double stackCap) {
        return detail::sizedCommitment(legal, denominations, player.roundCommitted, pot, fraction, stackCap, player.stack);
    };

    if (privateView.street == Street::preflop && headsUp) {
        return headsUpPreflopDecision(privateView, player, legal, denominations, analysis, profile_, variation);
    }

    if (legal.check) {
        // Heads-up, Virgo may take a small, safe value/protection bet with a
        // credible one-pair hand. Multiway thresholds remain unchanged.
        const auto valueBet = analysis.made >= (headsUp ? 0.50 : 0.68) && threat <= (headsUp ? 0.52 : 0.66);
        const auto protectedDraw = analysis.draw >= 0.44 && profile_.hope >= 0.55 && threat <= (headsUp ? 0.44 : 0.48);
        if (legal.bet && (valueBet || protectedDraw)) {
            const auto fraction = headsUp ? analysis.made >= 0.84 ? 0.54 : protectedDraw ? 0.36 : 0.40
                : analysis.made >= 0.84 ? 0.62 : protectedDraw ? 0.40 : 0.46;
            const auto stackCap = headsUp ? analysis.made >= 0.90 ? 0.66 : 0.45 : analysis.made >= 0.90 ? 0.72 : 0.50;
            return {.action = Action::bet, .amount = sizing(fraction, stackCap)};
        }
        return {.action = Action::check};
    }

    // In the narrow heads-up regime, large river pressure receives the full
    // defensive weight of Virgo's longevity preference. This deliberately
    // does not alter multiway calling behavior.
    const auto largeRiverBet = privateView.street == Street::river && headsUp && legal.call && price >= 0.38;
    if (largeRiverBet && analysis.made < 0.67 && legal.fold) return {.action = Action::fold};

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
