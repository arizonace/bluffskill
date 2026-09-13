#include "bluffskill/poker/leo_reference_player.hpp"

#include "reference_player_hand_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

enum class BetIntent { probe, value, protection, semiBluff, polar };

bool isAggressive(Action action) {
    return action == Action::bet || action == Action::raise;
}

Street previousStreet(Street street) {
    switch (street) {
    case Street::turn: return Street::flop;
    case Street::river: return Street::turn;
    default: return Street::waiting;
    }
}

bool madeAggressiveAction(const TableView& view, std::string_view player, Street street) {
    return std::ranges::any_of(view.actionHistory, [player, street](const auto& action) {
        return action.player == player && action.street == street && isAggressive(action.action);
    });
}

// A street is only considered a continuation opportunity when Leo's prior bet
// was actually called. This avoids treating a folded-out pot as a cue to fire
// again on a later street.
bool priorAggressionWasCalled(const TableView& view, std::string_view player, Street street) {
    bool ownAggression = false;
    for (const auto& action : view.actionHistory) {
        if (action.street != street) continue;
        if (action.player == player && isAggressive(action.action)) ownAggression = true;
        else if (ownAggression && action.action == Action::call) return true;
    }
    return false;
}

std::size_t preflopRaiseCount(const TableView& view) {
    return static_cast<std::size_t>(std::count_if(view.actionHistory.begin(), view.actionHistory.end(), [](const auto& action) {
        return action.street == Street::preflop && action.action == Action::raise;
    }));
}

std::size_t preflopLimpCount(const TableView& view) {
    return static_cast<std::size_t>(std::count_if(view.actionHistory.begin(), view.actionHistory.end(), [](const auto& action) {
        return action.street == Street::preflop && action.action == Action::call;
    }));
}

bool isHeadsUp(const detail::HandAnalysis& analysis) {
    return analysis.opponents == 1;
}

double effectiveStackInBigBlinds(const TableView& view, const TablePlayerView& player) {
    auto effective = player.stack + player.roundCommitted;
    for (const auto& candidate : view.players) {
        if (candidate.name == player.name || candidate.folded || (candidate.stack == 0 && candidate.committed == 0)) continue;
        effective = std::min(effective, candidate.stack + candidate.roundCommitted);
    }
    return static_cast<double>(effective) / static_cast<double>(std::max<Chips>(1, view.bigBlind));
}

Chips roundedCommitment(Chips desired, const LegalActions& legal, const ChipDenominations& denominations) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto unit = denominations.empty() ? Chips{1} : denominations.front();
    const auto rounded = ((std::max(desired, legal.minimumAmount) + unit - 1) / unit) * unit;
    return std::clamp(rounded, legal.minimumAmount, legal.maximumAmount);
}

Chips preflopRaiseSize(const TableView& view, const TablePlayerView& player, const LegalActions& legal,
    const ChipDenominations& denominations, std::size_t raises, std::size_t limpers, bool headsUp) {
    const auto bigBlind = std::max<Chips>(1, view.bigBlind);
    if (raises == 0) {
        // Small heads-up opens retain room to play postflop; multiway opens
        // charge the limpers rather than inviting the whole table in.
        const auto openBigBlinds = headsUp ? 2.3 : 2.7;
        const auto desired = static_cast<Chips>(std::llround(openBigBlinds * bigBlind)) + static_cast<Chips>(limpers) * bigBlind;
        return roundedCommitment(desired, legal, denominations);
    }

    // A re-raise is sized from the amount already opened, not the remaining
    // stack. The button/small blind acts first before the flop heads-up, so
    // it adds a modest out-of-position premium.
    const auto multiplier = headsUp && player.dealer ? 3.4 : 3.0;
    const auto desired = static_cast<Chips>(std::llround(view.currentBet * multiplier))
        + static_cast<Chips>(limpers) * bigBlind / 2;
    return roundedCommitment(desired, legal, denominations);
}

double sizingFraction(BetIntent intent, Street street, bool headsUp, double variation) {
    double fraction = 0.0;
    switch (intent) {
    case BetIntent::probe: fraction = 0.38; break;
    case BetIntent::value: fraction = 0.61; break;
    case BetIntent::protection: fraction = 0.70; break;
    case BetIntent::semiBluff: fraction = 0.56; break;
    case BetIntent::polar: fraction = 0.86; break;
    }
    if (street == Street::turn && intent != BetIntent::probe) fraction += 0.04;
    if (headsUp && intent == BetIntent::probe) fraction -= 0.04;
    // Variability selects a nearby, credible size; it never changes a probe
    // into a stack-sized wager.
    return std::clamp(fraction + variation * 0.06, 0.28, 0.95);
}

double stackCap(BetIntent intent, double made, double effectiveBigBlinds) {
    if (effectiveBigBlinds <= 12.0 && made >= 0.86) return 1.0;
    if (intent == BetIntent::polar || made >= 0.88) return 0.90;
    if (intent == BetIntent::protection) return 0.78;
    return 0.68;
}

ReferenceDecision preflopDecision(const TableView& view, const TablePlayerView& player, const LegalActions& legal,
    const ChipDenominations& denominations, const detail::HandAnalysis& analysis, const LeoReferencePlayerProfile& profile,
    double variation) {
    const auto headsUp = isHeadsUp(analysis);
    const auto raises = preflopRaiseCount(view);
    const auto limpers = preflopLimpCount(view);
    const auto score = std::clamp(analysis.preflop + profile.riskTolerance * 0.045 + profile.optimism * 0.025 + variation * 0.035, 0.0, 1.0);
    const auto effectiveBigBlinds = effectiveStackInBigBlinds(view, player);
    const auto foldOrCheck = [&] {
        if (legal.check) return ReferenceDecision{.action = Action::check};
        if (legal.fold) return ReferenceDecision{.action = Action::fold};
        if (legal.call) return ReferenceDecision{.action = Action::call};
        throw std::logic_error("Leo reference player has no legal preflop response");
    };
    const auto raise = [&] {
        return ReferenceDecision{.action = Action::raise,
            .amount = preflopRaiseSize(view, player, legal, denominations, raises, limpers, headsUp)};
    };

    if (raises == 0) {
        const auto openThreshold = (headsUp ? 0.43 : analysis.opponents <= 3 ? 0.54 : 0.60)
            - profile.riskTolerance * 0.07 - profile.optimism * 0.03;
        if (legal.raise && score >= openThreshold) return raise();
        // Complete the small blind only with hands just below the opening
        // range. Leo otherwise uses raise-or-fold discipline.
        if (headsUp && legal.call && score >= openThreshold - 0.05 && view.smallBlindSeat == player.seat) {
            return {.action = Action::call};
        }
        return foldOrCheck();
    }

    if (raises == 1) {
        const auto threeBetThreshold = (headsUp ? 0.69 : 0.78) - profile.riskTolerance * 0.06 - profile.optimism * 0.02;
        if (legal.raise && score >= threeBetThreshold) return raise();

        const auto callThreshold = (headsUp ? 0.52 : analysis.opponents <= 3 ? 0.62 : 0.68)
            - profile.riskTolerance * 0.04 + std::min(0.08, detail::potOdds(view, legal) * 0.20);
        if (legal.call && score >= callThreshold && effectiveBigBlinds >= 18.0) return {.action = Action::call};
        return foldOrCheck();
    }

    const auto squeezeThreshold = (headsUp ? 0.80 : 0.86) - profile.riskTolerance * 0.04;
    if (legal.raise && score >= squeezeThreshold) return raise();
    if (legal.call && headsUp && score >= 0.66 && effectiveBigBlinds >= 25.0) return {.action = Action::call};
    return foldOrCheck();
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
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    const auto variation = distribution(random) * profile_.variability;

    if (privateView.street == Street::preflop) {
        return preflopDecision(privateView, player, legal, denominations, analysis, profile_, variation);
    }

    const auto headsUp = isHeadsUp(analysis);
    const auto aggression = detail::observedAggression(privateView, player);
    const auto price = detail::potOdds(privateView, legal);
    const auto handValue = std::clamp(analysis.made + analysis.draw * 0.36 + profile_.optimism * 0.07 + variation * 0.05, 0.0, 1.0);
    const auto pressure = std::clamp(analysis.boardThreat + aggression * 0.45, 0.0, 1.0);
    const auto aggressiveThreshold = 0.67 - profile_.riskTolerance * 0.10 - profile_.optimism * 0.04 - (headsUp ? 0.04 : 0.0);
    const auto pot = detail::potSize(privateView);
    const auto effectiveBigBlinds = effectiveStackInBigBlinds(privateView, player);
    const auto sizing = [&](BetIntent intent) {
        return detail::sizedCommitment(legal, denominations, player.roundCommitted, pot,
            sizingFraction(intent, privateView.street, headsUp, variation), stackCap(intent, analysis.made, effectiveBigBlinds), player.stack);
    };
    const auto priorStreet = previousStreet(privateView.street);
    const auto continuing = priorStreet != Street::waiting
        && madeAggressiveAction(privateView, name(), priorStreet)
        && priorAggressionWasCalled(privateView, name(), priorStreet);

    if (legal.check) {
        bool bet = false;
        BetIntent intent = BetIntent::probe;
        if (privateView.street == Street::flop) {
            const auto continuationScore = handValue + profile_.riskTolerance * 0.08 + (headsUp ? 0.08 : 0.0) - pressure * 0.14;
            bet = analysis.made >= 0.56 || analysis.draw >= 0.36 || continuationScore >= aggressiveThreshold;
            intent = analysis.made >= 0.80 ? BetIntent::protection : analysis.draw >= 0.36 ? BetIntent::semiBluff
                : analysis.made >= 0.56 ? BetIntent::value : BetIntent::probe;
        } else if (privateView.street == Street::turn) {
            const auto barrel = analysis.made >= 0.63 || (analysis.draw >= 0.42 && pressure <= 0.48);
            bet = continuing ? barrel : analysis.made >= 0.71 || (headsUp && analysis.draw >= 0.44);
            intent = analysis.made >= 0.82 ? BetIntent::protection : analysis.draw >= 0.42 ? BetIntent::semiBluff : BetIntent::value;
        } else if (privateView.street == Street::river) {
            const auto value = analysis.made >= (continuing ? 0.69 : 0.75);
            const auto bluff = continuing && headsUp && analysis.made < 0.38 && analysis.preflop >= 0.62
                && pressure <= 0.26 && variation > 0.35;
            bet = value || bluff;
            intent = value || bluff ? BetIntent::polar : BetIntent::probe;
        }
        if (legal.bet && bet) return {.action = Action::bet, .amount = sizing(intent)};
        return {.action = Action::check};
    }

    // Facing aggression, Leo reserves turn and river raises for very strong
    // made hands. This prevents reflexively escalating with a merely
    // attractive draw.
    const auto raiseScore = handValue + profile_.riskTolerance * 0.13 - price * 0.20 - pressure * 0.08;
    const auto raiseMadeThreshold = privateView.street == Street::river ? 0.88 : privateView.street == Street::turn ? 0.84 : 0.78;
    const auto strongValue = analysis.made >= raiseMadeThreshold;
    const auto strongDraw = privateView.street == Street::flop && analysis.draw >= 0.48 && raiseScore >= aggressiveThreshold + 0.03;
    if (legal.raise && (strongValue || strongDraw) && raiseScore >= aggressiveThreshold) {
        return {.action = Action::raise, .amount = sizing(strongValue ? BetIntent::polar : BetIntent::semiBluff)};
    }

    const auto callScore = analysis.made + analysis.draw * 0.48 + profile_.riskTolerance * 0.06 + profile_.optimism * 0.04
        - pressure * 0.17 + variation * 0.03;
    if (legal.call && callScore >= price + 0.06) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Leo reference player has no legal response");
}

} // namespace bluffskill::poker
