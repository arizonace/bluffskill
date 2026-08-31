#include "bluffskill/poker/table.hpp"

#include <cassert>

int main() {
    using namespace bluffskill::poker;

    try {
        [[maybe_unused]] const auto invalidDenominations = normalizedChipDenominations({25, 100, 250});
        assert(false && "each denomination must divide the following denomination");
    } catch (const std::invalid_argument&) {
    }

    Table table("Red", 3, 200);
    table.seatPlayer("Alice", PlayerKind::api, 1, 50);
    table.seatPlayer("BotOne", PlayerKind::reference, 2, 200);
    table.seatPlayer("BotTwo", PlayerKind::reference, 3, 200);
    table.startHand();

    const auto publicView = table.viewFor();
    const auto aliceView = table.viewFor("Alice");
    assert(publicView.street == Street::preflop);
    assert(publicView.currentBet == 50);
    assert(publicView.smallBlind == 25);
    assert(publicView.bigBlind == 50);
    assert(publicView.roundsPlayed == 1);
    assert(publicView.actingSeat == 1);
    assert(publicView.smallBlindSeat == 2);
    assert(publicView.bigBlindSeat == 3);
    assert(publicView.actionHistory.size() == 2);
    assert(publicView.actionHistory[0].action == Action::smallBlind);
    assert(publicView.actionHistory[0].amount == 25);
    assert(publicView.actionHistory[1].action == Action::bigBlind);
    assert(publicView.actionHistory[1].amount == 50);
    assert(publicView.players.front().holeCards.empty());
    assert(aliceView.players.front().holeCards.size() == 2);
    assert(aliceView.legalActions && aliceView.legalActions->call);
    assert(!aliceView.legalActions->check);

    try {
        table.submitAction("Alice", Action::check, 0, aliceView.eventSequence);
        assert(false && "checking into the big blind must be rejected");
    } catch (const CommandError& error) {
        assert(error.failure() == CommandFailure::illegalAction);
        assert(table.viewFor("Alice").eventSequence == aliceView.eventSequence);
    }

    table.submitAction("Alice", Action::call, 0, aliceView.eventSequence);
    const auto botOneTurn = table.viewFor("BotOne");
    assert(botOneTurn.actingSeat == 2);
    assert(botOneTurn.legalActions && botOneTurn.legalActions->raise);
    assert((botOneTurn.chipDenominations == ChipDenominations{25, 100, 500, 1000}));

    try {
        table.submitAction("BotOne", Action::raise, 201, botOneTurn.eventSequence);
        assert(false && "a raise must be representable in active chip denominations");
    } catch (const CommandError& error) {
        assert(error.failure() == CommandFailure::illegalAction);
        assert(table.viewFor("BotOne").eventSequence == botOneTurn.eventSequence);
    }

    try {
        table.submitAction("BotOne", Action::call, 0, aliceView.eventSequence);
        assert(false && "a stale command must be rejected before mutating the hand");
    } catch (const CommandError& error) {
        assert(error.failure() == CommandFailure::staleSequence);
        assert(table.viewFor("BotOne").eventSequence == botOneTurn.eventSequence);
    }

    table.submitAction("BotOne", Action::raise, 200, botOneTurn.eventSequence);
    const auto botTwoTurn = table.viewFor("BotTwo");
    assert(botTwoTurn.actingSeat == 3);
    assert(botTwoTurn.currentBet == 200);
    assert(botTwoTurn.legalActions && botTwoTurn.legalActions->call);

    table.submitAction("BotTwo", Action::call, 0, botTwoTurn.eventSequence);
    const auto finished = table.viewFor("Alice");
    assert(finished.street == Street::showdown);
    assert(!finished.actingSeat);
    assert(finished.communityCards.size() == 5);
    assert(finished.pots.size() == 2);
    assert(finished.pots[0].amount == 150);
    assert(finished.pots[0].eligibleSeats.size() == 3);
    assert(finished.pots[1].amount == 300);
    assert((finished.pots[1].eligibleSeats == std::vector<std::size_t>{2, 3}));
    assert(finished.showdownOccurred);
    assert(finished.players[1].holeCards.size() == 2);
    assert(finished.players[2].holeCards.size() == 2);
    assert(!finished.players[0].showdownDescription.empty());
    assert(!finished.players[1].showdownDescription.empty());
    assert(!finished.players[2].showdownDescription.empty());
    assert(finished.payouts.size() == 2);
    assert(finished.payouts[0].amount == 150);
    assert(finished.payouts[1].amount == 300);

    Table configuredSmallBlind("Configured small blind", 2, 1'000,
        BlindSchedule{.smallBlind = 50});
    assert(configuredSmallBlind.viewFor().smallBlind == 50);
    assert(configuredSmallBlind.viewFor().bigBlind == 100);

    // Uneven action without an all-in remains one pot; side pots only cap an
    // all-in player's eligibility.
    Table noAllInSidePot("No all-in side pot", 3, 1000);
    noAllInSidePot.seatPlayer("Raise", PlayerKind::api, 1, 1000);
    noAllInSidePot.seatPlayer("CallOne", PlayerKind::api, 2, 1000);
    noAllInSidePot.seatPlayer("CallTwo", PlayerKind::api, 3, 1000);
    noAllInSidePot.startHand();
    auto noAllInView = noAllInSidePot.viewFor("Raise");
    noAllInSidePot.submitAction("Raise", Action::raise, 200, noAllInView.eventSequence);
    noAllInView = noAllInSidePot.viewFor("CallOne");
    noAllInSidePot.submitAction("CallOne", Action::call, 0, noAllInView.eventSequence);
    noAllInView = noAllInSidePot.viewFor("CallTwo");
    noAllInSidePot.submitAction("CallTwo", Action::call, 0, noAllInView.eventSequence);
    noAllInView = noAllInSidePot.viewFor();
    assert(noAllInView.street == Street::flop);
    assert(noAllInView.pots.size() == 1);
    assert(noAllInView.pots[0].amount == 600);

    Table rotation("Rotation", 3, 500);
    rotation.seatPlayer("One", PlayerKind::api, 1, 500);
    rotation.seatPlayer("Two", PlayerKind::api, 2, 500);
    rotation.seatPlayer("Three", PlayerKind::api, 3, 500);
    rotation.startHand();
    auto rotationView = rotation.viewFor("One");
    rotation.submitAction("One", Action::fold, 0, rotationView.eventSequence);
    rotationView = rotation.viewFor("Two");
    rotation.submitAction("Two", Action::fold, 0, rotationView.eventSequence);
    const auto foldedHand = rotation.viewFor();
    assert(foldedHand.street == Street::showdown);
    assert(!foldedHand.showdownOccurred);
    assert(foldedHand.payouts.size() == 1);
    assert(foldedHand.payouts[0].awards.size() == 1);
    assert(foldedHand.payouts[0].awards[0].seat == 3);
    assert(foldedHand.players[2].holeCards.empty());
    const auto foldedViewer = rotation.viewFor("One");
    assert(foldedViewer.players[0].holeCards.size() == 2);
    assert(!foldedViewer.players[0].showdownDescription.empty());
    rotation.startNextHand();
    const auto nextHand = rotation.viewFor();
    assert(nextHand.street == Street::preflop);
    assert(nextHand.roundsPlayed == 2);
    assert(nextHand.dealerSeat == 2);
    assert(nextHand.smallBlindSeat == 3);

    rotation.restartGame();
    const auto restarted = rotation.viewFor();
    assert(restarted.street == Street::preflop);
    assert(restarted.roundsPlayed == 1);
    assert(restarted.dealerSeat == 1);
    assert(restarted.players[0].stack == 500);
    assert(restarted.players[1].stack == 475);
    assert(restarted.players[2].stack == 450);

    Table blindLevels("Blind levels", 3, 1000, BlindSchedule{.handsPerLevel = 1, .minutesPerLevel = std::chrono::minutes{60}});
    blindLevels.seatPlayer("A", PlayerKind::api, 1, 1000);
    blindLevels.seatPlayer("B", PlayerKind::api, 2, 1000);
    blindLevels.seatPlayer("C", PlayerKind::api, 3, 1000);
    blindLevels.startHand();
    assert(blindLevels.viewFor().smallBlind == 25);
    auto blindView = blindLevels.viewFor("A");
    blindLevels.submitAction("A", Action::fold, 0, blindView.eventSequence);
    blindView = blindLevels.viewFor("B");
    blindLevels.submitAction("B", Action::fold, 0, blindView.eventSequence);
    blindLevels.startNextHand();
    assert(blindLevels.viewFor().blindLevel == 1);
    assert(blindLevels.viewFor().smallBlind == 50);
    assert(blindLevels.viewFor().bigBlind == 100);

    // The big blind starts the raise amount. Each successive minimum raise is
    // the current bet plus the size of the preceding full raise.
    Table minimumRaises("Minimum raises", 3, 2'000);
    minimumRaises.seatPlayer("Opener", PlayerKind::api, 1, 2'000);
    minimumRaises.seatPlayer("Raiser", PlayerKind::api, 2, 2'000);
    minimumRaises.seatPlayer("Big", PlayerKind::api, 3, 2'000);
    minimumRaises.startHand();
    auto raiseView = minimumRaises.viewFor("Opener");
    assert(raiseView.legalActions && raiseView.legalActions->minimumAmount == 100);
    minimumRaises.submitAction("Opener", Action::raise, 300, raiseView.eventSequence);
    raiseView = minimumRaises.viewFor("Raiser");
    assert(raiseView.legalActions && raiseView.legalActions->minimumAmount == 550);
    try {
        minimumRaises.submitAction("Raiser", Action::raise, 400, raiseView.eventSequence);
        assert(false && "a raise smaller than the previous full raise must be rejected");
    } catch (const CommandError& error) {
        assert(error.failure() == CommandFailure::illegalAction);
    }
    minimumRaises.submitAction("Raiser", Action::raise, 550, raiseView.eventSequence);
    raiseView = minimumRaises.viewFor("Big");
    assert(raiseView.legalActions && raiseView.legalActions->minimumAmount == 800);
    try {
        minimumRaises.submitAction("Big", Action::raise, 600, raiseView.eventSequence);
        assert(false && "a re-raise must match the preceding raise amount");
    } catch (const CommandError& error) {
        assert(error.failure() == CommandFailure::illegalAction);
    }

    // A player may make a smaller raise only when that exact amount is all-in.
    Table shortAllIn("Short all-in", 3, 2'000);
    shortAllIn.seatPlayer("Opener", PlayerKind::api, 1, 2'000);
    shortAllIn.seatPlayer("AllIn", PlayerKind::api, 2, 400);
    shortAllIn.seatPlayer("Big", PlayerKind::api, 3, 2'000);
    shortAllIn.startHand();
    auto allInView = shortAllIn.viewFor("Opener");
    shortAllIn.submitAction("Opener", Action::raise, 300, allInView.eventSequence);
    allInView = shortAllIn.viewFor("AllIn");
    assert(allInView.legalActions && allInView.legalActions->minimumAmount == 400);
    assert(allInView.legalActions->maximumAmount == 400);
    shortAllIn.submitAction("AllIn", Action::raise, 400, allInView.eventSequence);
}
