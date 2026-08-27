#pragma once

#include "bluffskill/cards/card.hpp"
#include "bluffskill/cards/deck.hpp"
#include "bluffskill/poker/player.hpp"

#include <cstdint>
#include <chrono>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bluffskill::poker {

using Chips = std::int64_t;

struct BlindSchedule {
    std::size_t handsPerLevel{16};
    std::chrono::minutes minutesPerLevel{20};
};

enum class Street { waiting, preflop, flop, turn, river, showdown };
enum class Action { smallBlind, bigBlind, check, call, bet, raise, fold };
enum class CommandFailure { staleSequence, turnConflict, illegalAction };

class CommandError final : public std::runtime_error {
public:
    CommandError(CommandFailure failure, std::string message) : std::runtime_error(std::move(message)), failure_(failure) {}
    [[nodiscard]] CommandFailure failure() const noexcept { return failure_; }

private:
    CommandFailure failure_;
};

struct LegalActions {
    bool check{false};
    bool call{false};
    bool bet{false};
    bool raise{false};
    bool fold{false};
    Chips callAmount{0};
    Chips minimumAmount{0}; // Total commitment for a bet or raise this round.
    Chips maximumAmount{0};
};

struct PotView {
    Chips amount{0};
    std::vector<std::size_t> eligibleSeats;
};

struct PayoutAwardView {
    std::size_t seat{0};
    Chips amount{0};
};

// A settled main pot or side pot.  Awards are explicit so a split pot remains
// visible without asking a client to recreate the chip-splitting rule.
struct PayoutView {
    Chips amount{0};
    std::vector<PayoutAwardView> awards;
};

struct TablePlayerView {
    std::string name;
    PlayerKind kind{PlayerKind::api};
    std::size_t seat{0};
    Chips stack{0};
    Chips committed{0};
    bool folded{false};
    bool dealer{false};
    bool acting{false};
    std::vector<cards::Card> holeCards; // Populated only for the requested viewer.
};

struct ActionView {
    std::string player;
    std::size_t seat{0};
    Street street{Street::waiting};
    Action action{Action::check};
    Chips amount{0};
};

struct TableView {
    std::string name;
    std::uint64_t eventSequence{0};
    Street street{Street::waiting};
    Chips currentBet{0}; // Largest commitment in the active betting round.
    Chips smallBlind{0};
    Chips bigBlind{0};
    std::size_t blindLevel{0};
    std::optional<std::size_t> dealerSeat;
    std::optional<std::size_t> smallBlindSeat;
    std::optional<std::size_t> bigBlindSeat;
    std::optional<std::size_t> actingSeat;
    std::vector<cards::Card> communityCards;
    std::vector<TablePlayerView> players;
    std::vector<PotView> pots;
    std::vector<PayoutView> payouts;
    bool showdownOccurred{false};
    std::vector<ActionView> actionHistory;
    std::optional<LegalActions> legalActions; // Available only to the viewer who is acting.
};

[[nodiscard]] std::string_view toString(Street street) noexcept;
[[nodiscard]] std::string_view toString(Action action) noexcept;

// Table is the poker-action serialization boundary. It has no Qt or transport dependency.
class Table {
public:
    Table(std::string name, std::size_t maximumSeats, Chips startingStack, BlindSchedule blindSchedule = {});
    ~Table();

    void seatPlayer(std::string name, PlayerKind kind, std::size_t seat, Chips stack);
    void startHand();
    void startNextHand();
    void restartGame();
    void setBlindSchedule(BlindSchedule blindSchedule);
    [[nodiscard]] TableView viewFor(std::string_view viewerName = {}) const;
    [[nodiscard]] std::uint64_t eventSequence() const noexcept { return eventSequence_; }
    void submitAction(std::string_view playerName, Action action, Chips amount, std::uint64_t expectedSequence);

private:
    struct Seat;

    [[nodiscard]] Seat& seatFor(std::string_view name);
    [[nodiscard]] const Seat* seatFor(std::string_view name) const;
    [[nodiscard]] std::vector<Seat*> liveSeats();
    [[nodiscard]] std::vector<const Seat*> liveSeats() const;
    [[nodiscard]] std::vector<Seat*> eligibleSeats();
    [[nodiscard]] Seat* nextLiveSeatAfter(std::size_t seat);
    [[nodiscard]] Seat* nextEligibleSeatAfter(std::size_t seat);
    [[nodiscard]] Seat* nextPendingSeatAfter(std::size_t seat) const;
    [[nodiscard]] LegalActions legalActionsFor(const Seat& seat) const;
    [[nodiscard]] std::vector<PotView> pots() const;
    void postBlind(Seat& seat, Chips amount, Action action);
    void drawCommunityCards(std::size_t count);
    void setRoundPendingAfterDealer();
    void advanceAfterAction(Seat& actor, bool fullRaise);
    void advanceStreet();
    void updateActor();
    void finishByFold();
    void settleShowdown();
    void advanceBlindLevelIfDue();
    [[nodiscard]] Chips smallBlindAmount() const;
    [[nodiscard]] Chips bigBlindAmount() const;
    [[nodiscard]] bool hasSingleLiveSeat() const;

    std::string name_;
    std::size_t maximumSeats_{0};
    Chips startingStack_{0};
    BlindSchedule blindSchedule_;
    std::size_t blindLevel_{0};
    std::size_t handsAtCurrentBlindLevel_{0};
    bool blindClockStarted_{false};
    std::chrono::steady_clock::time_point blindLevelStartedAt_{std::chrono::steady_clock::now()};
    std::vector<Seat> seats_;
    Street street_{Street::waiting};
    std::optional<std::size_t> dealerSeat_;
    std::optional<std::size_t> smallBlindSeat_;
    std::optional<std::size_t> bigBlindSeat_;
    std::optional<std::size_t> actingSeat_;
    Chips currentBet_{0};
    Chips lastFullRaise_{0};
    std::vector<cards::Card> communityCards_;
    std::vector<ActionView> history_;
    std::vector<PayoutView> payouts_;
    bool showdownOccurred_{false};
    std::uint64_t eventSequence_{0};
    std::optional<cards::Deck> deck_;
    std::mt19937_64 random_{std::random_device{}()};
};

} // namespace bluffskill::poker
