#include "bluffskill/poker/libra_reference_player.hpp"

#include "reference_player_hand_analysis.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <random>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

enum class Position { early, middle, late, button, blind };

bool isHeadsUp(const detail::HandAnalysis& analysis) {
    return analysis.opponents == 1;
}

std::size_t preflopRaiseCount(const TableView& view) {
    return static_cast<std::size_t>(std::count_if(view.actionHistory.begin(), view.actionHistory.end(), [](const auto& action) {
        return action.street == Street::preflop && action.action == Action::raise;
    }));
}

std::vector<const TablePlayerView*> livePlayers(const TableView& view) {
    std::vector<const TablePlayerView*> players;
    for (const auto& candidate : view.players) {
        if (!candidate.folded && (candidate.stack > 0 || candidate.committed > 0)) players.push_back(&candidate);
    }
    std::ranges::sort(players, {}, &TablePlayerView::seat);
    return players;
}

Position positionFor(const TableView& view, const TablePlayerView& player) {
    const auto players = livePlayers(view);
    if (players.size() <= 2) return player.dealer ? Position::button : Position::blind;
    const auto dealer = std::ranges::find_if(players, [](const auto candidate) { return candidate->dealer; });
    const auto actor = std::ranges::find(players, &player);
    if (dealer == players.end() || actor == players.end()) return Position::early;
    const auto count = players.size();
    const auto dealerIndex = static_cast<std::size_t>(std::distance(players.begin(), dealer));
    const auto actorIndex = static_cast<std::size_t>(std::distance(players.begin(), actor));
    const auto offset = (actorIndex + count - dealerIndex) % count;
    if (offset == 0) return Position::button;
    if (offset <= 2) return Position::blind;
    if (offset <= 4) return Position::early;
    if (offset + 2 < count) return Position::middle;
    return Position::late;
}

double openThreshold(std::size_t players, Position position) {
    if (players <= 2) return position == Position::button ? 0.46 : 0.55;
    if (players <= 4) {
        switch (position) {
        case Position::button: return 0.48;
        case Position::late: return 0.52;
        case Position::middle: return 0.57;
        case Position::blind: return 0.60;
        case Position::early: return 0.63;
        }
    }
    switch (position) {
    case Position::button: return 0.48;
    case Position::late: return 0.54;
    case Position::middle: return 0.61;
    case Position::blind: return 0.60;
    case Position::early: return 0.68;
    }
    return 1.0;
}

Chips roundedCommitment(Chips desired, const LegalActions& legal, const ChipDenominations& denominations) {
    if (legal.maximumAmount <= legal.minimumAmount) return legal.minimumAmount;
    const auto unit = denominations.empty() ? Chips{1} : denominations.front();
    const auto rounded = ((std::max(desired, legal.minimumAmount) + unit - 1) / unit) * unit;
    return std::clamp(rounded, legal.minimumAmount, legal.maximumAmount);
}

double effectiveStackInBigBlinds(const TableView& view, const TablePlayerView& player) {
    auto effective = player.stack + player.roundCommitted;
    for (const auto* candidate : livePlayers(view)) {
        if (candidate->name != player.name) effective = std::min(effective, candidate->stack + candidate->roundCommitted);
    }
    return static_cast<double>(effective) / static_cast<double>(std::max<Chips>(1, view.bigBlind));
}

ReferenceDecision foldOrCheck(const LegalActions& legal) {
    if (legal.check) return {.action = Action::check};
    if (legal.fold) return {.action = Action::fold};
    if (legal.call) return {.action = Action::call};
    throw std::logic_error("Libra reference player has no legal response");
}

ReferenceDecision preflopDecision(const TableView& view, const TablePlayerView& player, const LegalActions& legal,
    const ChipDenominations& denominations, const detail::HandAnalysis& analysis) {
    const auto players = livePlayers(view);
    const auto playerCount = players.size();
    const auto headsUp = isHeadsUp(analysis);
    const auto position = positionFor(view, player);
    const auto raises = preflopRaiseCount(view);
    const auto strength = analysis.preflop;
    const auto effectiveBigBlinds = effectiveStackInBigBlinds(view, player);
    const auto raise = [&](bool shortStack) {
        if (shortStack) return ReferenceDecision{.action = Action::raise, .amount = legal.maximumAmount};
        const auto bigBlind = std::max<Chips>(1, view.bigBlind);
        const auto desired = raises == 0
            ? static_cast<Chips>(std::llround(2.5 * bigBlind))
            : static_cast<Chips>(std::llround(view.currentBet * (position == Position::blind ? 3.5 : 3.0)));
        return ReferenceDecision{.action = Action::raise, .amount = roundedCommitment(desired, legal, denominations)};
    };

    // Libra's short-stack chart is deliberately simple: it uses only legal
    // all-in raises, avoids limps, and tightens as more players remain.
    if (effectiveBigBlinds <= 10.0) {
        const auto shortOpen = (headsUp ? 0.46 : playerCount <= 4 ? 0.56 : 0.66);
        const auto shortRaise = headsUp ? 0.70 : 0.80;
        if (legal.raise && strength >= (raises == 0 ? shortOpen : shortRaise)) return raise(true);
        if (legal.call && raises == 1 && strength >= (headsUp ? 0.59 : 0.68)) return {.action = Action::call};
        return foldOrCheck(legal);
    }

    if (raises == 0) {
        if (legal.raise && strength >= openThreshold(playerCount, position)) return raise(false);
        return foldOrCheck(legal);
    }

    if (raises == 1) {
        const auto threeBetThreshold = headsUp ? 0.79 : 0.84;
        const auto callThreshold = headsUp ? 0.60 : playerCount <= 4 ? 0.66 : 0.71;
        if (legal.raise && strength >= threeBetThreshold) return raise(false);
        if (legal.call && strength >= callThreshold) return {.action = Action::call};
        return foldOrCheck(legal);
    }

    if (legal.raise && strength >= 0.92) return raise(false);
    if (legal.call && strength >= (headsUp ? 0.73 : 0.78) && detail::potOdds(view, legal) <= 0.22) {
        return {.action = Action::call};
    }
    return foldOrCheck(legal);
}

bool hasFlush(const std::vector<cards::Card>& cards) {
    std::array<int, 4> suits{};
    for (const auto card : cards) ++suits[static_cast<std::size_t>(card.suit)];
    return std::ranges::any_of(suits, [](int count) { return count >= 5; });
}

bool hasStraight(const std::vector<cards::Card>& cards) {
    std::array<bool, 15> ranks{};
    for (const auto card : cards) ranks[static_cast<std::size_t>(card.rank)] = true;
    for (int high = 14; high >= 5; --high) {
        bool complete = true;
        for (int rank = high; rank > high - 5; --rank) complete = complete && ranks[rank];
        if (complete) return true;
    }
    return ranks[14] && ranks[2] && ranks[3] && ranks[4] && ranks[5];
}

double directDrawEquity(const TableView& view, const TablePlayerView& player) {
    if (view.communityCards.size() < 3 || view.communityCards.size() > 4 || player.holeCards.size() != 2) return 0.0;
    std::vector<cards::Card> known = view.communityCards;
    known.insert(known.end(), player.holeCards.begin(), player.holeCards.end());
    const auto alreadyStraight = hasStraight(known);
    const auto alreadyFlush = hasFlush(known);
    if (alreadyStraight || alreadyFlush) return 0.0;
    int outs = 0;
    for (int suit = static_cast<int>(cards::Suit::clubs); suit <= static_cast<int>(cards::Suit::spades); ++suit) {
        for (int rank = static_cast<int>(cards::Rank::two); rank <= static_cast<int>(cards::Rank::ace); ++rank) {
            const cards::Card candidate{static_cast<cards::Suit>(suit), static_cast<cards::Rank>(rank)};
            if (std::ranges::find(known, candidate) != known.end()) continue;
            auto next = known;
            next.push_back(candidate);
            if (hasStraight(next) || hasFlush(next)) ++outs;
        }
    }
    const auto unseen = 52 - static_cast<int>(known.size());
    if (outs == 0 || unseen <= 0) return 0.0;
    if (view.communityCards.size() == 4) return static_cast<double>(outs) / unseen;
    const auto misses = unseen - outs;
    return 1.0 - static_cast<double>(misses * std::max(0, misses - 1)) / static_cast<double>(unseen * (unseen - 1));
}

struct HandRank {
    int category{-1};
    std::vector<int> tiebreakers;
    auto operator<=>(const HandRank&) const = default;
};

int straightHigh(const std::array<int, 15>& counts) {
    for (int high = 14; high >= 5; --high) {
        bool complete = true;
        for (int rank = high; rank > high - 5; --rank) complete = complete && counts[rank] > 0;
        if (complete) return high;
    }
    return counts[14] && counts[2] && counts[3] && counts[4] && counts[5] ? 5 : 0;
}

HandRank rankFive(const std::array<cards::Card, 5>& cards) {
    std::array<int, 15> counts{};
    std::array<int, 5> ranks{};
    for (std::size_t index = 0; index < cards.size(); ++index) {
        ranks[index] = static_cast<int>(cards[index].rank);
        ++counts[ranks[index]];
    }
    std::ranges::sort(ranks, std::greater<>{});
    const auto flush = std::ranges::all_of(cards, [&cards](const cards::Card& card) { return card.suit == cards.front().suit; });
    const auto straight = straightHigh(counts);
    if (flush && straight) return {8, {straight}};

    int four = 0;
    std::vector<int> triples;
    std::vector<int> pairs;
    std::vector<int> singles;
    for (int rank = 14; rank >= 2; --rank) {
        if (counts[rank] == 4) four = rank;
        else if (counts[rank] == 3) triples.push_back(rank);
        else if (counts[rank] == 2) pairs.push_back(rank);
        else if (counts[rank] == 1) singles.push_back(rank);
    }
    if (four) return {7, {four, singles.front()}};
    if (!triples.empty() && (!pairs.empty() || triples.size() > 1)) {
        return {6, {triples.front(), triples.size() > 1 ? triples[1] : pairs.front()}};
    }
    if (flush) return {5, {ranks.begin(), ranks.end()}};
    if (straight) return {4, {straight}};
    if (!triples.empty()) return {3, {triples.front(), singles[0], singles[1]}};
    if (pairs.size() >= 2) return {2, {pairs[0], pairs[1], singles.front()}};
    if (pairs.size() == 1) return {1, {pairs.front(), singles[0], singles[1], singles[2]}};
    return {0, {ranks.begin(), ranks.end()}};
}

HandRank bestHand(const std::vector<cards::Card>& cards) {
    HandRank best;
    for (std::size_t a = 0; a < cards.size(); ++a) for (std::size_t b = a + 1; b < cards.size(); ++b)
    for (std::size_t c = b + 1; c < cards.size(); ++c) for (std::size_t d = c + 1; d < cards.size(); ++d)
    for (std::size_t e = d + 1; e < cards.size(); ++e) {
        const auto candidate = rankFive({cards[a], cards[b], cards[c], cards[d], cards[e]});
        if (candidate > best) best = candidate;
    }
    return best;
}

std::uint64_t publicStateSeed(const TableView& view, const TablePlayerView& player) {
    auto seed = std::uint64_t{1469598103934665603ULL};
    const auto combine = [&seed](std::uint64_t value) { seed = (seed ^ value) * 1099511628211ULL; };
    for (const auto card : player.holeCards) combine(static_cast<std::uint64_t>(static_cast<int>(card.suit) * 16 + static_cast<int>(card.rank)));
    for (const auto card : view.communityCards) combine(static_cast<std::uint64_t>(static_cast<int>(card.suit) * 16 + static_cast<int>(card.rank)));
    for (const auto& action : view.actionHistory) {
        combine(static_cast<std::uint64_t>(action.seat));
        combine(static_cast<std::uint64_t>(action.action));
        combine(static_cast<std::uint64_t>(action.amount));
    }
    return seed;
}

double rangeFloor(const TableView& view, std::string_view opponent) {
    std::size_t raises = 0;
    double floor = 0.0;
    for (const auto& action : view.actionHistory) {
        if (action.street != Street::preflop) continue;
        if (action.player == opponent) {
            if (action.action == Action::raise) floor = std::max(floor, raises == 0 ? 0.58 : 0.76);
            else if (action.action == Action::call) floor = std::max(floor, 0.35);
        }
        if (action.action == Action::raise) ++raises;
    }
    return floor;
}

std::vector<cards::Card> fullDeck() {
    std::vector<cards::Card> deck;
    deck.reserve(52);
    for (int suit = static_cast<int>(cards::Suit::clubs); suit <= static_cast<int>(cards::Suit::spades); ++suit) {
        for (int rank = static_cast<int>(cards::Rank::two); rank <= static_cast<int>(cards::Rank::ace); ++rank) {
            deck.push_back({static_cast<cards::Suit>(suit), static_cast<cards::Rank>(rank)});
        }
    }
    return deck;
}

bool containsCard(const std::vector<cards::Card>& cards, cards::Card candidate) {
    return std::ranges::find(cards, candidate) != cards.end();
}

double sampledShowdownEquity(const TableView& view, const TablePlayerView& player) {
    constexpr int samples = 128;
    const auto opponents = livePlayers(view);
    std::vector<const TablePlayerView*> activeOpponents;
    for (const auto* candidate : opponents) if (candidate->name != player.name) activeOpponents.push_back(candidate);
    if (activeOpponents.empty() || player.holeCards.size() != 2) return 0.0;

    const auto deck = fullDeck();
    std::vector<std::vector<std::array<cards::Card, 2>>> rangeCandidates;
    for (const auto* opponent : activeOpponents) {
        std::vector<std::array<cards::Card, 2>> candidates;
        const auto floor = rangeFloor(view, opponent->name);
        for (std::size_t first = 0; first < deck.size(); ++first) for (std::size_t second = first + 1; second < deck.size(); ++second) {
            if (detail::preflopStrength({deck[first], deck[second]}) >= floor) candidates.push_back({deck[first], deck[second]});
        }
        rangeCandidates.push_back(std::move(candidates));
    }

    std::mt19937_64 random(publicStateSeed(view, player));
    double equity = 0.0;
    for (int sample = 0; sample < samples; ++sample) {
        std::vector<cards::Card> used = player.holeCards;
        used.insert(used.end(), view.communityCards.begin(), view.communityCards.end());
        std::vector<std::vector<cards::Card>> opponentHoles;
        bool viable = true;
        for (std::size_t opponentIndex = 0; opponentIndex < activeOpponents.size(); ++opponentIndex) {
            const auto& candidates = rangeCandidates.at(opponentIndex);
            if (candidates.empty()) { viable = false; break; }
            const auto available = [&used](const auto& candidate) {
                return !containsCard(used, candidate[0]) && !containsCard(used, candidate[1]);
            };
            const std::array<cards::Card, 2>* chosen = nullptr;
            for (int attempt = 0; attempt < 32; ++attempt) {
                const auto& candidate = candidates.at(std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(random));
                if (available(candidate)) { chosen = &candidate; break; }
            }
            if (!chosen) {
                const auto found = std::ranges::find_if(candidates, available);
                if (found != candidates.end()) chosen = &*found;
            }
            if (!chosen) { viable = false; break; }
            opponentHoles.push_back({(*chosen)[0], (*chosen)[1]});
            used.push_back((*chosen)[0]);
            used.push_back((*chosen)[1]);
        }
        if (!viable) continue;

        auto remaining = fullDeck();
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(), [&used](const auto card) { return containsCard(used, card); }), remaining.end());
        std::ranges::shuffle(remaining, random);
        auto board = view.communityCards;
        for (std::size_t index = 0; board.size() < 5 && index < remaining.size(); ++index) board.push_back(remaining[index]);
        if (board.size() != 5) continue;

        auto heroCards = board;
        heroCards.insert(heroCards.end(), player.holeCards.begin(), player.holeCards.end());
        const auto hero = bestHand(heroCards);
        std::size_t winners = 1;
        bool heroWins = true;
        for (const auto& holes : opponentHoles) {
            auto opponentCards = board;
            opponentCards.insert(opponentCards.end(), holes.begin(), holes.end());
            const auto opponent = bestHand(opponentCards);
            if (opponent > hero) { heroWins = false; break; }
            if (opponent == hero) ++winners;
        }
        if (heroWins) equity += 1.0 / static_cast<double>(winners);
    }
    return equity / samples;
}

double valueBetThreshold(Street street, std::size_t opponents) {
    const auto base = street == Street::river ? 0.72 : 0.70;
    return std::clamp(base + 0.04 * static_cast<double>(opponents > 0 ? opponents - 1 : 0), 0.0, 0.90);
}

} // namespace

LibraReferencePlayer::LibraReferencePlayer(std::string name) : ReferencePlayerController(std::move(name)) {}

std::vector<ReferencePlayerParameter> LibraReferencePlayer::parameters() const {
    return {{"Canon revision", 1.0}, {"Bluffing", 0.0}, {"Direct draw odds", 1.0}};
}

ReferenceDecision LibraReferencePlayer::chooseResponse(const TableView& privateView, std::mt19937_64& random) const {
    (void)random;
    if (!privateView.legalActions) throw std::invalid_argument("Libra reference player was asked to act without legal actions");
    const auto& player = detail::actingPlayer(privateView, name());
    const auto& legal = *privateView.legalActions;
    const auto denominations = privateView.chipDenominations.empty() ? defaultChipDenominations() : privateView.chipDenominations;
    const auto analysis = detail::analyzeHand(privateView, player);

    if (privateView.street == Street::preflop) {
        return preflopDecision(privateView, player, legal, denominations, analysis);
    }

    const auto pot = detail::potSize(privateView);
    const auto sizing = [&](double fraction, double stackCap) {
        return detail::sizedCommitment(legal, denominations, player.roundCommitted, pot, fraction, stackCap, player.stack);
    };
    const auto opponents = analysis.opponents;
    if (legal.check) {
        // A bet is value only: draws are checked, even when a semi-bluff would
        // be profitable against a folding range.
        if (legal.bet && analysis.made >= valueBetThreshold(privateView.street, opponents)) {
            const auto fraction = privateView.street == Street::flop ? 0.50 : privateView.street == Street::turn ? 0.67 : 0.75;
            const auto stackCap = privateView.street == Street::river ? 0.80 : 0.68;
            return {.action = Action::bet, .amount = sizing(fraction, stackCap)};
        }
        return {.action = Action::check};
    }

    // Raises are reserved for very strong made hands; Libra never turns an
    // unmade draw into a semi-bluff.
    if (legal.raise && analysis.made >= 0.90) {
        return {.action = Action::raise, .amount = sizing(0.75, 0.82)};
    }

    auto equity = std::max(sampledShowdownEquity(privateView, player), directDrawEquity(privateView, player));
    if (!player.dealer && privateView.street != Street::river) equity = std::max(0.0, equity - 0.03);
    const auto price = detail::potOdds(privateView, legal);
    if (legal.call && equity >= price + 0.03) return {.action = Action::call};
    if (legal.fold) return {.action = Action::fold};
    throw std::logic_error("Libra reference player has no legal response");
}

} // namespace bluffskill::poker
