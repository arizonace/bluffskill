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
}
