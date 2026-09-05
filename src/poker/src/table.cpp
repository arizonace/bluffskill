#include "bluffskill/poker/table.hpp"

#include "bluffskill/cards/deck.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <ranges>
#include <vector>

namespace bluffskill::poker {

namespace {

bool sameName(std::string_view left, std::string_view right) {
    return left == right;
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

std::string rankName(int rank) {
    return cards::toString(static_cast<cards::Rank>(rank));
}

std::string joinRanks(const std::vector<int>& ranks, std::size_t first = 0) {
    std::string result;
    for (std::size_t index = first; index < ranks.size(); ++index) {
        if (!result.empty()) result += ',';
        result += rankName(ranks[index]);
    }
    return result;
}

std::string describeHand(const HandRank& rank) {
    const auto& values = rank.tiebreakers;
    if (values.empty()) return {};
    switch (rank.category) {
    case 8: return "Straight Flush(" + rankName(values[0]) + ')';
    case 7: return "Quad(" + rankName(values[0]) + "), " + joinRanks(values, 1);
    case 6: return "Full-House(" + rankName(values[0]) + ',' + rankName(values[1]) + ')';
    case 5: return "Flush(" + rankName(values[0]) + ')';
    case 4: return "Straight(" + rankName(values[0]) + ')';
    case 3: return "Set(" + rankName(values[0]) + "), " + joinRanks(values, 1);
    case 2: return "Two Pair(" + rankName(values[0]) + ',' + rankName(values[1]) + "), " + joinRanks(values, 2);
    case 1: return "Pair(" + rankName(values[0]) + "), " + joinRanks(values, 1);
    case 0: return "High(" + rankName(values[0]) + "), " + joinRanks(values, 1);
    default: return {};
    }
}

std::string describePartialHand(const std::vector<cards::Card>& cards) {
    std::array<int, 15> counts{};
    for (const auto card : cards) ++counts[static_cast<int>(card.rank)];
    std::vector<int> pairs;
    std::vector<int> singles;
    int triple = 0;
    int quad = 0;
    for (int rank = 14; rank >= 2; --rank) {
        if (counts[rank] == 4) quad = rank;
        else if (counts[rank] == 3) triple = rank;
        else if (counts[rank] == 2) pairs.push_back(rank);
        else if (counts[rank] == 1) singles.push_back(rank);
    }
    if (quad) return "Quad(" + rankName(quad) + ")" + (singles.empty() ? "" : ", " + joinRanks(singles));
    if (triple) return "Set(" + rankName(triple) + ")" + (singles.empty() ? "" : ", " + joinRanks(singles));
    if (pairs.size() >= 2) return "Two Pair(" + rankName(pairs[0]) + ',' + rankName(pairs[1]) + ")" + (singles.empty() ? "" : ", " + joinRanks(singles));
    if (pairs.size() == 1) return "Pair(" + rankName(pairs[0]) + ")" + (singles.empty() ? "" : ", " + joinRanks(singles));
    return singles.empty() ? std::string{} : "High(" + rankName(singles[0]) + ")" + (singles.size() == 1 ? "" : ", " + joinRanks(singles, 1));
}

std::string describeAvailableHand(const std::vector<cards::Card>& cards) {
    return cards.size() >= 5 ? describeHand(bestHand(cards)) : describePartialHand(cards);
}

} // namespace

ChipDenominations defaultChipDenominations() {
    return {25, 100, 500, 1000};
}

ChipDenominations normalizedChipDenominations(ChipDenominations denominations) {
    if (denominations.empty()) throw std::invalid_argument("at least one chip denomination is required");
    if (std::ranges::any_of(denominations, [](Chips denomination) { return denomination <= 0; })) {
        throw std::invalid_argument("chip denominations must be positive");
    }
    std::ranges::sort(denominations);
    denominations.erase(std::unique(denominations.begin(), denominations.end()), denominations.end());
    for (std::size_t index = 1; index < denominations.size(); ++index) {
        if (denominations[index] % denominations[index - 1] != 0) {
            throw std::invalid_argument("each chip denomination must be an integer multiple of the preceding denomination");
        }
    }
    return denominations;
}

struct Table::Seat {
    std::string name;
    PlayerKind kind{PlayerKind::api};
    std::size_t number{0};
    Chips stack{0};
    Chips handCommitted{0};
    Chips roundCommitted{0};
    bool folded{false};
    bool allIn{false};
    bool pending{false};
    bool canRaise{true};
    std::vector<cards::Card> holeCards;
};

std::string_view toString(Street street) noexcept {
    switch (street) {
    case Street::waiting: return "Waiting";
    case Street::preflop: return "Preflop";
    case Street::flop: return "Flop";
    case Street::turn: return "Turn";
    case Street::river: return "River";
    case Street::showdown: return "Showdown";
    }
    return "Unknown";
}

std::string_view toString(Action action) noexcept {
    switch (action) {
    case Action::check: return "Check";
    case Action::call: return "Call";
    case Action::bet: return "Bet";
    case Action::raise: return "Raise";
    case Action::fold: return "Fold";
    case Action::smallBlind: return "Small Blind";
    case Action::bigBlind: return "Big Blind";
    }
    return "Unknown";
}

Table::Table(std::string name, std::size_t maximumSeats, Chips startingStack, BlindSchedule blindSchedule,
    ChipDenominations chipDenominations)
    : name_(std::move(name)), maximumSeats_(maximumSeats), startingStack_(startingStack),
      chipDenominations_(normalizedChipDenominations(std::move(chipDenominations))), blindSchedule_(blindSchedule) {
    if (maximumSeats_ == 0 || startingStack_ <= 0) throw std::invalid_argument("table requires seats and a positive starting stack");
    if (!isChipValue(startingStack_)) {
        throw std::invalid_argument("starting stack must be a multiple of the smallest chip denomination");
    }
    setBlindSchedule(blindSchedule_);
}

Table::~Table() = default;

void Table::seatPlayer(std::string name, PlayerKind kind, std::size_t seat, Chips stack) {
    if (street_ != Street::waiting) throw std::logic_error("players cannot be seated during a hand");
    if (seat == 0 || seat > maximumSeats_ || stack <= 0 || !isChipValue(stack)) throw std::invalid_argument("invalid table seat or stack");
    if (std::ranges::any_of(seats_, [seat, &name](const Seat& candidate) { return candidate.number == seat || sameName(candidate.name, name); })) {
        throw std::invalid_argument("seat or player is already occupied");
    }
    seats_.push_back({.name = std::move(name), .kind = kind, .number = seat, .stack = stack});
    std::ranges::sort(seats_, {}, &Seat::number);
}

std::vector<Table::Seat*> Table::liveSeats() {
    std::vector<Seat*> result;
    for (auto& seat : seats_) if (!seat.folded && (street_ == Street::waiting || seat.stack > 0 || seat.handCommitted > 0)) result.push_back(&seat);
    return result;
}

std::vector<const Table::Seat*> Table::liveSeats() const {
    std::vector<const Seat*> result;
    for (const auto& seat : seats_) if (!seat.folded && (street_ == Street::waiting || seat.stack > 0 || seat.handCommitted > 0)) result.push_back(&seat);
    return result;
}

std::vector<Table::Seat*> Table::eligibleSeats() {
    std::vector<Seat*> result;
    for (auto& seat : seats_) if (seat.stack > 0) result.push_back(&seat);
    return result;
}

Table::Seat* Table::nextLiveSeatAfter(std::size_t seat) {
    const auto live = liveSeats();
    if (live.empty()) return nullptr;
    const auto next = std::ranges::find_if(live, [seat](const Seat* candidate) { return candidate->number > seat; });
    return next == live.end() ? live.front() : *next;
}

Table::Seat* Table::nextEligibleSeatAfter(std::size_t seat) {
    const auto eligible = eligibleSeats();
    if (eligible.empty()) return nullptr;
    const auto next = std::ranges::find_if(eligible, [seat](const Seat* candidate) { return candidate->number > seat; });
    return next == eligible.end() ? eligible.front() : *next;
}

Table::Seat* Table::nextPendingSeatAfter(std::size_t seat) const {
    const auto first = std::ranges::find_if(seats_, [seat](const Seat& candidate) { return candidate.number > seat && candidate.pending; });
    if (first != seats_.end()) return const_cast<Seat*>(&*first);
    const auto wrap = std::ranges::find_if(seats_, [](const Seat& candidate) { return candidate.pending; });
    return wrap == seats_.end() ? nullptr : const_cast<Seat*>(&*wrap);
}

void Table::postBlind(Seat& seat, Chips amount, Action action) {
    const auto paid = std::min(amount, seat.stack);
    seat.stack -= paid;
    seat.handCommitted += paid;
    seat.roundCommitted += paid;
    seat.allIn = seat.stack == 0;
    history_.push_back({.player = seat.name, .seat = seat.number, .street = Street::preflop, .action = action, .amount = paid,
        .stackAfter = seat.stack});
}

void Table::setBlindSchedule(BlindSchedule blindSchedule) {
    if (blindSchedule.handsPerLevel == 0 || blindSchedule.minutesPerLevel.count() <= 0) {
        throw std::invalid_argument("blind schedule requires positive hand and minute intervals");
    }
    if (blindSchedule.smallBlind == 0) {
        blindSchedule.smallBlind = blindSchedule_.smallBlind == 0 ? chipDenominations_.front() : blindSchedule_.smallBlind;
    }
    if (blindSchedule.smallBlind < 0 || !isChipValue(blindSchedule.smallBlind)) {
        throw std::invalid_argument("small blind must be a positive multiple of the smallest chip denomination");
    }
    blindSchedule_ = blindSchedule;
}

Chips Table::smallBlindAmount() const {
    const auto multiplier = static_cast<Chips>(1) << std::min<std::size_t>(blindLevel_, 20);
    return blindSchedule_.smallBlind * multiplier;
}

Chips Table::bigBlindAmount() const {
    const auto multiplier = static_cast<Chips>(1) << std::min<std::size_t>(blindLevel_, 20);
    return blindSchedule_.smallBlind * 2 * multiplier;
}

bool Table::isChipValue(Chips amount) const noexcept {
    return amount >= 0 && amount % chipDenominations_.front() == 0;
}

void Table::advanceBlindLevelIfDue() {
    const auto now = std::chrono::steady_clock::now();
    if (!blindClockStarted_) {
        blindLevelStartedAt_ = now;
        return;
    }
    if (handsAtCurrentBlindLevel_ < blindSchedule_.handsPerLevel
        && now - blindLevelStartedAt_ < blindSchedule_.minutesPerLevel) return;
    ++blindLevel_;
    handsAtCurrentBlindLevel_ = 0;
    blindLevelStartedAt_ = now;
}

void Table::startHand() {
    if (street_ != Street::waiting) throw std::logic_error("a hand is already running");
    if (seats_.size() < 2) throw std::logic_error("at least two seated players are required");
    advanceBlindLevelIfDue();
    for (auto& seat : seats_) {
        seat.handCommitted = 0;
        seat.roundCommitted = 0;
        seat.folded = seat.stack == 0;
        seat.allIn = seat.stack == 0;
        seat.pending = false;
        seat.canRaise = true;
        seat.holeCards.clear();
    }
    const auto live = liveSeats();
    if (live.size() < 2) throw std::logic_error("at least two players with chips are required");
    const auto dealerStillEligible = dealerSeat_ && std::ranges::any_of(live, [this](const Seat* seat) { return seat->number == *dealerSeat_; });
    if (!dealerStillEligible) dealerSeat_ = live.front()->number;
    auto* small = nextLiveSeatAfter(*dealerSeat_);
    auto* big = nextLiveSeatAfter(small->number);
    if (live.size() == 2) {
        small = const_cast<Seat*>(live.front());
        big = nextLiveSeatAfter(small->number);
    }

    deck_.emplace();
    deck_->shuffle(random_);
    communityCards_.clear();
    history_.clear();
    payouts_.clear();
    showdownOccurred_ = false;
    street_ = Street::preflop;
    for (auto& seat : seats_) {
        if (!seat.folded) seat.holeCards.reserve(2);
    }
    for (int round = 0; round < 2; ++round) {
        for (auto& seat : seats_) {
            if (seat.folded) continue;
            const auto card = deck_->draw();
            if (!card) throw std::logic_error("deck was exhausted during the deal");
            seat.holeCards.push_back(*card);
        }
    }
    smallBlindSeat_ = small->number;
    bigBlindSeat_ = big->number;
    postBlind(*small, smallBlindAmount(), Action::smallBlind);
    postBlind(*big, bigBlindAmount(), Action::bigBlind);
    currentBet_ = big->roundCommitted;
    lastFullRaise_ = bigBlindAmount();
    for (auto& seat : seats_) seat.pending = !seat.folded && !seat.allIn;
    if (const auto* actor = nextPendingSeatAfter(big->number)) actingSeat_ = actor->number;
    else advanceStreet();
    ++handsAtCurrentBlindLevel_;
    ++roundsPlayed_;
    blindClockStarted_ = true;
    ++eventSequence_;
}

void Table::startNextHand() {
    if (street_ != Street::showdown) throw std::logic_error("the current hand has not finished");
    if (!dealerSeat_) throw std::logic_error("the finished hand has no dealer");
    const auto* nextDealer = nextEligibleSeatAfter(*dealerSeat_);
    if (nextDealer == nullptr) throw std::logic_error("at least two players with chips are required");
    dealerSeat_ = nextDealer->number;
    street_ = Street::waiting;
    startHand();
}

void Table::restartGame() {
    for (auto& seat : seats_) {
        seat.stack = startingStack_;
        seat.handCommitted = 0;
        seat.roundCommitted = 0;
        seat.folded = false;
        seat.allIn = false;
        seat.pending = false;
        seat.canRaise = true;
        seat.holeCards.clear();
    }
    street_ = Street::waiting;
    dealerSeat_.reset();
    smallBlindSeat_.reset();
    bigBlindSeat_.reset();
    actingSeat_.reset();
    currentBet_ = 0;
    lastFullRaise_ = 0;
    communityCards_.clear();
    history_.clear();
    payouts_.clear();
    showdownOccurred_ = false;
    blindLevel_ = 0;
    handsAtCurrentBlindLevel_ = 0;
    roundsPlayed_ = 0;
    blindClockStarted_ = false;
    blindLevelStartedAt_ = std::chrono::steady_clock::now();
    deck_.reset();
    startHand();
}

Table::Seat& Table::seatFor(std::string_view name) {
    const auto found = std::ranges::find_if(seats_, [name](const Seat& candidate) { return sameName(candidate.name, name); });
    if (found == seats_.end()) throw CommandError(CommandFailure::turnConflict, "player is not seated at this table");
    return *found;
}

const Table::Seat* Table::seatFor(std::string_view name) const {
    const auto found = std::ranges::find_if(seats_, [name](const Seat& candidate) { return sameName(candidate.name, name); });
    return found == seats_.end() ? nullptr : &*found;
}

LegalActions Table::legalActionsFor(const Seat& seat) const {
    LegalActions legal;
    if (!actingSeat_ || *actingSeat_ != seat.number || seat.folded || seat.allIn) return legal;
    const auto owed = std::max<Chips>(0, currentBet_ - seat.roundCommitted);
    legal.check = owed == 0;
    legal.call = owed > 0 && seat.stack > 0;
    legal.fold = true;
    legal.callAmount = std::min(owed, seat.stack);
    legal.maximumAmount = seat.roundCommitted + seat.stack;
    if (currentBet_ == 0) {
        legal.bet = legal.maximumAmount >= bigBlindAmount();
        legal.minimumAmount = bigBlindAmount();
    } else {
        const auto fullRaiseTo = currentBet_ + lastFullRaise_;
        legal.raise = seat.canRaise && legal.maximumAmount > currentBet_;
        legal.minimumAmount = legal.maximumAmount < fullRaiseTo ? legal.maximumAmount : fullRaiseTo;
    }
    return legal;
}

std::vector<PotView> Table::pots() const {
    const auto totalCommitted = std::accumulate(seats_.begin(), seats_.end(), Chips{0}, [](Chips total, const Seat& seat) {
        return total + seat.handCommitted;
    });
    if (totalCommitted == 0) return {};

    // Uneven commitments only form side pots when an all-in player caps what
    // they can contest.  Otherwise all contributed chips remain one pot.
    std::vector<Chips> levels;
    for (const auto& seat : seats_) {
        if (seat.allIn && seat.handCommitted > 0) levels.push_back(seat.handCommitted);
    }
    std::ranges::sort(levels);
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    if (levels.empty()) {
        PotView pot{.amount = totalCommitted};
        for (const auto& seat : seats_) if (!seat.folded && seat.handCommitted > 0) pot.eligibleSeats.push_back(seat.number);
        return {std::move(pot)};
    }

    const auto highestCommitment = std::ranges::max(seats_, {}, &Seat::handCommitted).handCommitted;
    if (highestCommitment > levels.back()) levels.push_back(highestCommitment);
    std::vector<PotView> result;
    Chips previous = 0;
    for (const auto level : levels) {
        const auto contributors = std::count_if(seats_.begin(), seats_.end(), [level](const Seat& seat) { return seat.handCommitted >= level; });
        // A folded player may have committed less than the first all-in cap.
        // They are not eligible to win, but every chip they put in must remain
        // in the corresponding pot layer.  Counting only seats at `level`
        // would otherwise drop that smaller contribution entirely.
        const auto amount = std::accumulate(seats_.begin(), seats_.end(), Chips{0}, [previous, level](Chips total, const Seat& seat) {
            return total + std::min(seat.handCommitted, level) - std::min(seat.handCommitted, previous);
        });
        // A level above an all-in cap is a side pot only if continued action
        // actually supplied more than one contribution at that level.
        if (amount > 0 && (level <= levels.front() || contributors >= 2)) {
            PotView pot{.amount = amount};
            for (const auto& seat : seats_) if (seat.handCommitted >= level && !seat.folded) pot.eligibleSeats.push_back(seat.number);
            result.push_back(std::move(pot));
        }
        previous = level;
    }
    return result;
}

TableView Table::viewFor(std::string_view viewerName) const {
    TableView view{.name = name_, .eventSequence = eventSequence_, .street = street_, .currentBet = currentBet_,
        .smallBlind = smallBlindAmount(), .bigBlind = bigBlindAmount(), .chipDenominations = chipDenominations_,
        .blindLevel = blindLevel_, .roundsPlayed = roundsPlayed_, .dealerSeat = dealerSeat_,
        .smallBlindSeat = smallBlindSeat_, .bigBlindSeat = bigBlindSeat_, .actingSeat = actingSeat_, .communityCards = communityCards_,
        .pots = pots(), .payouts = payouts_, .showdownOccurred = showdownOccurred_, .actionHistory = history_};
    const auto* viewer = seatFor(viewerName);
    for (const auto& seat : seats_) {
        TablePlayerView player{.name = seat.name, .kind = seat.kind, .seat = seat.number, .stack = seat.stack, .committed = seat.handCommitted,
                               .roundCommitted = seat.roundCommitted,
                               .folded = seat.folded, .dealer = dealerSeat_ && *dealerSeat_ == seat.number,
                               .acting = actingSeat_ && *actingSeat_ == seat.number};
        if (viewer == &seat || (showdownOccurred_ && !seat.folded)) player.holeCards = seat.holeCards;
        if ((showdownOccurred_ && !seat.folded) || (viewer == &seat && street_ == Street::showdown)) {
            std::vector<cards::Card> cards = communityCards_;
            cards.insert(cards.end(), seat.holeCards.begin(), seat.holeCards.end());
            player.showdownDescription = describeAvailableHand(cards);
        }
        view.players.push_back(std::move(player));
    }
    if (viewer != nullptr) {
        const auto legal = legalActionsFor(*viewer);
        if (legal.check || legal.call || legal.bet || legal.raise || legal.fold) view.legalActions = legal;
    }
    return view;
}

bool Table::hasSingleLiveSeat() const { return liveSeats().size() == 1; }

void Table::returnUncalledContribution() {
    if (seats_.size() < 2) return;
    const auto highest = std::ranges::max(seats_, {}, &Seat::handCommitted).handCommitted;
    const auto contributors = std::count_if(seats_.begin(), seats_.end(), [highest](const Seat& seat) {
        return seat.handCommitted == highest;
    });
    if (highest == 0 || contributors != 1) return;
    const auto otherHighest = std::ranges::max(seats_ | std::views::filter([highest](const Seat& seat) {
        return seat.handCommitted != highest;
    }), {}, &Seat::handCommitted).handCommitted;
    const auto refund = highest - otherHighest;
    if (refund == 0) return;
    auto& seat = *std::ranges::find_if(seats_, [highest](const Seat& candidate) { return candidate.handCommitted == highest; });
    seat.stack += refund;
    seat.handCommitted -= refund;
    seat.roundCommitted = std::min(seat.roundCommitted, seat.handCommitted);
    seat.allIn = false;
}

void Table::finishByFold() {
    returnUncalledContribution();
    const auto live = liveSeats();
    if (live.size() != 1) return;
    Chips winnings = 0;
    for (const auto& pot : pots()) winnings += pot.amount;
    live.front()->stack += winnings;
    payouts_.clear();
    if (winnings > 0) payouts_.push_back({.amount = winnings, .awards = {{.seat = live.front()->number, .amount = winnings}}});
    showdownOccurred_ = false;
    street_ = Street::showdown;
    actingSeat_.reset();
    for (auto& seat : seats_) seat.pending = false;
}

void Table::settleShowdown() {
    if (communityCards_.size() != 5) throw std::logic_error("showdown requires five community cards");
    returnUncalledContribution();
    payouts_.clear();
    for (const auto& pot : pots()) {
        std::vector<Seat*> eligible;
        for (const auto seatNumber : pot.eligibleSeats) {
            const auto found = std::ranges::find_if(seats_, [seatNumber](const Seat& seat) { return seat.number == seatNumber; });
            if (found != seats_.end()) eligible.push_back(const_cast<Seat*>(&*found));
        }
        if (eligible.empty()) continue;
        HandRank winningRank;
        std::vector<Seat*> winners;
        for (auto* seat : eligible) {
            std::vector<cards::Card> cards = communityCards_;
            cards.insert(cards.end(), seat->holeCards.begin(), seat->holeCards.end());
            const auto rank = bestHand(cards);
            if (rank > winningRank) { winningRank = rank; winners = {seat}; }
            else if (rank == winningRank) winners.push_back(seat);
        }
        std::ranges::sort(winners, [this](const Seat* left, const Seat* right) {
            const auto distance = [this](const Seat* seat) { return (seat->number + maximumSeats_ - *dealerSeat_) % maximumSeats_; };
            return distance(left) < distance(right);
        });
        const auto winnerCount = static_cast<Chips>(winners.size());
        const auto chipUnit = chipDenominations_.front();
        const auto share = (pot.amount / winnerCount / chipUnit) * chipUnit;
        auto remainder = (pot.amount - share * winnerCount) / chipUnit;
        PayoutView payout{.amount = pot.amount};
        for (auto* winner : winners) {
            const auto amount = share + (remainder-- > 0 ? chipUnit : 0);
            winner->stack += amount;
            payout.awards.push_back({.seat = winner->number, .amount = amount});
        }
        payouts_.push_back(std::move(payout));
    }
    showdownOccurred_ = true;
}

void Table::drawCommunityCards(std::size_t count) {
    if (!deck_ || !deck_->draw()) throw std::logic_error("deck was exhausted during a hand"); // Burn card.
    for (std::size_t index = 0; index < count; ++index) {
        const auto card = deck_->draw();
        if (!card) throw std::logic_error("deck was exhausted during a hand");
        communityCards_.push_back(*card);
    }
}

void Table::setRoundPendingAfterDealer() {
    for (auto& seat : seats_) {
        seat.roundCommitted = 0;
        seat.pending = !seat.folded && !seat.allIn;
        seat.canRaise = true;
    }
    currentBet_ = 0;
    lastFullRaise_ = bigBlindAmount();
    updateActor();
}

void Table::advanceStreet() {
    switch (street_) {
    case Street::preflop: street_ = Street::flop; drawCommunityCards(3); setRoundPendingAfterDealer(); break;
    case Street::flop: street_ = Street::turn; drawCommunityCards(1); setRoundPendingAfterDealer(); break;
    case Street::turn: street_ = Street::river; drawCommunityCards(1); setRoundPendingAfterDealer(); break;
    case Street::river:
        street_ = Street::showdown;
        actingSeat_.reset();
        settleShowdown();
        break;
    default: break;
    }
}

void Table::updateActor() {
    if (!dealerSeat_) { actingSeat_.reset(); return; }
    const auto* next = nextPendingSeatAfter(*dealerSeat_);
    if (next == nullptr) advanceStreet();
    else actingSeat_ = next->number;
}

void Table::advanceAfterAction(Seat& actor, bool fullRaise) {
    if (hasSingleLiveSeat()) { finishByFold(); return; }
    actor.pending = false;
    actor.canRaise = false;
    if (fullRaise) {
        for (auto& seat : seats_) {
            seat.pending = !seat.folded && !seat.allIn && seat.number != actor.number;
            if (seat.number != actor.number && !seat.folded && !seat.allIn) seat.canRaise = true;
        }
    } else {
        for (auto& seat : seats_) {
            if (!seat.folded && !seat.allIn && seat.roundCommitted < currentBet_) seat.pending = true;
        }
    }
    const auto* next = nextPendingSeatAfter(actor.number);
    if (next == nullptr) advanceStreet();
    else actingSeat_ = next->number;
}

void Table::submitAction(std::string_view playerName, Action action, Chips amount, std::uint64_t expectedSequence) {
    if (expectedSequence != eventSequence_) throw CommandError(CommandFailure::staleSequence, "table state is stale; refresh the table view");
    if (street_ == Street::waiting || street_ == Street::showdown || !actingSeat_) throw CommandError(CommandFailure::turnConflict, "the table is not accepting actions");
    auto& actor = seatFor(playerName);
    if (actor.number != *actingSeat_) throw CommandError(CommandFailure::turnConflict, "it is not this player's turn");
    const auto legal = legalActionsFor(actor);
    const auto owed = std::max<Chips>(0, currentBet_ - actor.roundCommitted);
    Chips committed = 0;
    bool fullRaise = false;

    switch (action) {
    case Action::smallBlind:
    case Action::bigBlind:
        throw CommandError(CommandFailure::illegalAction, "blind posts are assigned by the table, not submitted by a player");
    case Action::check:
        if (!legal.check) throw CommandError(CommandFailure::illegalAction, "check is only legal when no chips are owed");
        break;
    case Action::call:
        if (!legal.call) throw CommandError(CommandFailure::illegalAction, "call is not legal now");
        committed = std::min(owed, actor.stack);
        actor.stack -= committed;
        actor.handCommitted += committed;
        actor.roundCommitted += committed;
        actor.allIn = actor.stack == 0;
        break;
    case Action::bet:
        if (!legal.bet || amount < legal.minimumAmount || amount > legal.maximumAmount || !isChipValue(amount)) {
            throw CommandError(CommandFailure::illegalAction, "bet amount is outside the legal range");
        }
        committed = amount - actor.roundCommitted;
        actor.stack -= committed;
        actor.handCommitted += committed;
        actor.roundCommitted = amount;
        actor.allIn = actor.stack == 0;
        currentBet_ = amount;
        lastFullRaise_ = amount;
        fullRaise = true;
        break;
    case Action::raise:
        if (!legal.raise || amount < legal.minimumAmount || amount > legal.maximumAmount || !isChipValue(amount)) {
            throw CommandError(CommandFailure::illegalAction, "raise amount is outside the legal range");
        }
        committed = amount - actor.roundCommitted;
        actor.stack -= committed;
        actor.handCommitted += committed;
        actor.roundCommitted = amount;
        actor.allIn = actor.stack == 0;
        fullRaise = amount >= currentBet_ + lastFullRaise_;
        if (fullRaise) lastFullRaise_ = amount - currentBet_;
        currentBet_ = amount;
        break;
    case Action::fold:
        if (!legal.fold) throw CommandError(CommandFailure::illegalAction, "fold is not legal now");
        actor.folded = true;
        break;
    }

    history_.push_back({.player = actor.name, .seat = actor.number, .street = street_, .action = action, .amount = committed,
        .stackAfter = actor.stack});
    ++eventSequence_;
    advanceAfterAction(actor, fullRaise);
}

} // namespace bluffskill::poker
