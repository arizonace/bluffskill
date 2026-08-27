#include "bluffskill/poker/table.hpp"

#include <cassert>

int main() {
    using namespace bluffskill::poker;

    Table table("Red", 3, 200);
    table.seatPlayer("Alice", PlayerKind::api, 1, 100);
    table.seatPlayer("BotOne", PlayerKind::reference, 2, 200);
    table.seatPlayer("BotTwo", PlayerKind::reference, 3, 200);
    table.startHand();

    const auto publicView = table.viewFor();
    const auto aliceView = table.viewFor("Alice");
    assert(publicView.street == Street::preflop);
    assert(publicView.currentBet == 100);
    assert(publicView.actingSeat == 1);
    assert(publicView.smallBlindSeat == 2);
    assert(publicView.bigBlindSeat == 3);
    assert(publicView.actionHistory.size() == 2);
    assert(publicView.actionHistory[0].action == Action::smallBlind);
    assert(publicView.actionHistory[0].amount == 50);
    assert(publicView.actionHistory[1].action == Action::bigBlind);
    assert(publicView.actionHistory[1].amount == 100);
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
    assert(finished.pots[0].amount == 300);
    assert(finished.pots[0].eligibleSeats.size() == 3);
    assert(finished.pots[1].amount == 200);
    assert((finished.pots[1].eligibleSeats == std::vector<std::size_t>{2, 3}));
    assert(finished.showdownOccurred);
    assert(finished.players[1].holeCards.size() == 2);
    assert(finished.players[2].holeCards.size() == 2);
    assert(finished.payouts.size() == 2);
    assert(finished.payouts[0].amount == 300);
    assert(finished.payouts[1].amount == 200);

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
    rotation.startNextHand();
    const auto nextHand = rotation.viewFor();
    assert(nextHand.street == Street::preflop);
    assert(nextHand.dealerSeat == 2);
    assert(nextHand.smallBlindSeat == 3);

    rotation.restartGame();
    const auto restarted = rotation.viewFor();
    assert(restarted.street == Street::preflop);
    assert(restarted.dealerSeat == 1);
    assert(restarted.players[0].stack == 500);
    assert(restarted.players[1].stack == 450);
    assert(restarted.players[2].stack == 400);

    Table blindLevels("Blind levels", 3, 1000, BlindSchedule{.handsPerLevel = 1, .minutesPerLevel = std::chrono::minutes{60}});
    blindLevels.seatPlayer("A", PlayerKind::api, 1, 1000);
    blindLevels.seatPlayer("B", PlayerKind::api, 2, 1000);
    blindLevels.seatPlayer("C", PlayerKind::api, 3, 1000);
    blindLevels.startHand();
    assert(blindLevels.viewFor().smallBlind == 50);
    auto blindView = blindLevels.viewFor("A");
    blindLevels.submitAction("A", Action::fold, 0, blindView.eventSequence);
    blindView = blindLevels.viewFor("B");
    blindLevels.submitAction("B", Action::fold, 0, blindView.eventSequence);
    blindLevels.startNextHand();
    assert(blindLevels.viewFor().blindLevel == 1);
    assert(blindLevels.viewFor().smallBlind == 100);
    assert(blindLevels.viewFor().bigBlind == 200);
}
