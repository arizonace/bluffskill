#include "bluffskill/poker/table.hpp"

#include "bluffskill/cards/deck.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace bluffskill::poker {

namespace {

constexpr Chips smallBlind = 50;
constexpr Chips bigBlind = 100;

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

} // namespace

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

Table::Table(std::string name, std::size_t maximumSeats, Chips startingStack)
    : name_(std::move(name)), maximumSeats_(maximumSeats), startingStack_(startingStack) {
    if (maximumSeats_ == 0 || startingStack_ <= 0) throw std::invalid_argument("table requires seats and a positive starting stack");
}

Table::~Table() = default;

void Table::seatPlayer(std::string name, PlayerKind kind, std::size_t seat, Chips stack) {
    if (street_ != Street::waiting) throw std::logic_error("players cannot be seated during a hand");
    if (seat == 0 || seat > maximumSeats_ || stack <= 0) throw std::invalid_argument("invalid table seat or stack");
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
    history_.push_back({.player = seat.name, .seat = seat.number, .street = Street::preflop, .action = action, .amount = paid});
}

void Table::startHand() {
    if (street_ != Street::waiting) throw std::logic_error("a hand is already running");
    if (seats_.size() < 2) throw std::logic_error("at least two seated players are required");
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
    postBlind(*small, smallBlind, Action::smallBlind);
    postBlind(*big, bigBlind, Action::bigBlind);
    currentBet_ = big->roundCommitted;
    lastFullRaise_ = bigBlind;
    for (auto& seat : seats_) seat.pending = !seat.folded && !seat.allIn;
    if (const auto* actor = nextPendingSeatAfter(big->number)) actingSeat_ = actor->number;
    else advanceStreet();
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
        legal.bet = legal.maximumAmount >= bigBlind;
        legal.minimumAmount = bigBlind;
    } else {
        const auto fullRaiseTo = currentBet_ + lastFullRaise_;
        legal.raise = seat.canRaise && legal.maximumAmount > currentBet_;
        legal.minimumAmount = legal.maximumAmount < fullRaiseTo ? legal.maximumAmount : fullRaiseTo;
    }
    return legal;
}

std::vector<PotView> Table::pots() const {
    std::vector<Chips> levels;
    for (const auto& seat : seats_) if (seat.handCommitted > 0) levels.push_back(seat.handCommitted);
    std::ranges::sort(levels);
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    std::vector<PotView> result;
    Chips previous = 0;
    for (const auto level : levels) {
        const auto contributors = std::count_if(seats_.begin(), seats_.end(), [level](const Seat& seat) { return seat.handCommitted >= level; });
        const auto amount = (level - previous) * contributors;
        if (amount > 0) {
            PotView pot{.amount = amount};
            for (const auto& seat : seats_) if (seat.handCommitted >= level && !seat.folded) pot.eligibleSeats.push_back(seat.number);
            result.push_back(std::move(pot));
        }
        previous = level;
    }
    return result;
}

TableView Table::viewFor(std::string_view viewerName) const {
    TableView view{.name = name_, .eventSequence = eventSequence_, .street = street_, .currentBet = currentBet_, .dealerSeat = dealerSeat_,
        .smallBlindSeat = smallBlindSeat_, .bigBlindSeat = bigBlindSeat_, .actingSeat = actingSeat_, .communityCards = communityCards_,
        .pots = pots(), .payouts = payouts_, .showdownOccurred = showdownOccurred_, .actionHistory = history_};
    const auto* viewer = seatFor(viewerName);
    for (const auto& seat : seats_) {
        TablePlayerView player{.name = seat.name, .kind = seat.kind, .seat = seat.number, .stack = seat.stack, .committed = seat.handCommitted,
                               .folded = seat.folded, .dealer = dealerSeat_ && *dealerSeat_ == seat.number,
                               .acting = actingSeat_ && *actingSeat_ == seat.number};
        if (viewer == &seat || (showdownOccurred_ && !seat.folded)) player.holeCards = seat.holeCards;
        view.players.push_back(std::move(player));
    }
    if (viewer != nullptr) {
        const auto legal = legalActionsFor(*viewer);
        if (legal.check || legal.call || legal.bet || legal.raise || legal.fold) view.legalActions = legal;
    }
    return view;
}

bool Table::hasSingleLiveSeat() const { return liveSeats().size() == 1; }

void Table::finishByFold() {
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
        const auto share = pot.amount / static_cast<Chips>(winners.size());
        auto remainder = pot.amount % static_cast<Chips>(winners.size());
        PayoutView payout{.amount = pot.amount};
        for (auto* winner : winners) {
            const auto amount = share + (remainder-- > 0 ? 1 : 0);
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
    lastFullRaise_ = bigBlind;
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
        if (!legal.bet || amount < legal.minimumAmount || amount > legal.maximumAmount) {
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
        if (!legal.raise || amount < legal.minimumAmount || amount > legal.maximumAmount) {
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

    history_.push_back({.player = actor.name, .seat = actor.number, .street = street_, .action = action, .amount = committed});
    ++eventSequence_;
    advanceAfterAction(actor, fullRaise);
}

} // namespace bluffskill::poker
