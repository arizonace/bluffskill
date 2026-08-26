#include "bluffskill/poker/table.hpp"

#include "bluffskill/cards/deck.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <set>

namespace bluffskill::poker {

namespace {

constexpr Chips smallBlind = 50;
constexpr Chips bigBlind = 100;

bool sameName(std::string_view left, std::string_view right) {
    return left == right;
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

Table::Seat* Table::nextLiveSeatAfter(std::size_t seat) {
    const auto live = liveSeats();
    if (live.empty()) return nullptr;
    const auto next = std::ranges::find_if(live, [seat](const Seat* candidate) { return candidate->number > seat; });
    return next == live.end() ? live.front() : *next;
}

Table::Seat* Table::nextPendingSeatAfter(std::size_t seat) const {
    const auto first = std::ranges::find_if(seats_, [seat](const Seat& candidate) { return candidate.number > seat && candidate.pending; });
    if (first != seats_.end()) return const_cast<Seat*>(&*first);
    const auto wrap = std::ranges::find_if(seats_, [](const Seat& candidate) { return candidate.pending; });
    return wrap == seats_.end() ? nullptr : const_cast<Seat*>(&*wrap);
}

void Table::postBlind(Seat& seat, Chips amount) {
    const auto paid = std::min(amount, seat.stack);
    seat.stack -= paid;
    seat.handCommitted += paid;
    seat.roundCommitted += paid;
    seat.allIn = seat.stack == 0;
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
    dealerSeat_ = live.front()->number;
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
    postBlind(*small, smallBlind);
    postBlind(*big, bigBlind);
    currentBet_ = big->roundCommitted;
    lastFullRaise_ = bigBlind;
    street_ = Street::preflop;
    for (auto& seat : seats_) seat.pending = !seat.folded && !seat.allIn;
    if (const auto* actor = nextPendingSeatAfter(big->number)) actingSeat_ = actor->number;
    else advanceStreet();
    ++eventSequence_;
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
    TableView view{.name = name_, .eventSequence = eventSequence_, .street = street_, .dealerSeat = dealerSeat_, .actingSeat = actingSeat_, .communityCards = communityCards_, .pots = pots(), .actionHistory = history_};
    const auto* viewer = seatFor(viewerName);
    for (const auto& seat : seats_) {
        TablePlayerView player{.name = seat.name, .kind = seat.kind, .seat = seat.number, .stack = seat.stack, .committed = seat.handCommitted,
                               .folded = seat.folded, .dealer = dealerSeat_ && *dealerSeat_ == seat.number,
                               .acting = actingSeat_ && *actingSeat_ == seat.number};
        if (viewer == &seat) player.holeCards = seat.holeCards;
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
    street_ = Street::showdown;
    actingSeat_.reset();
    for (auto& seat : seats_) seat.pending = false;
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
        street_ = Street::showdown; // Hand ranking and payout are deliberately a later engine slice.
        actingSeat_.reset();
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
