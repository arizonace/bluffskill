#include "bluffskill/poker/table.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <string>
#include <vector>

namespace {

using namespace bluffskill::poker;

Chips stackTotal(const TableView& view) {
    return std::accumulate(view.players.begin(), view.players.end(), Chips{0},
        [](Chips total, const TablePlayerView& player) { return total + player.stack; });
}

std::size_t playersWithChips(const TableView& view) {
    return static_cast<std::size_t>(std::count_if(view.players.begin(), view.players.end(),
        [](const TablePlayerView& player) { return player.stack > 0; }));
}

void assertChipInvariant(const TableView& view, Chips expectedTotal) {
    const auto committed = std::accumulate(view.players.begin(), view.players.end(), Chips{0},
        [](Chips total, const TablePlayerView& player) { return total + player.committed; });
    // Before a hand is settled, the pot is represented by commitments removed
    // from stacks.  Once it is settled, its awards are already in stacks.
    assert((view.street == Street::showdown ? stackTotal(view) : stackTotal(view) + committed) == expectedTotal);
}

// Drive one complete hand through the same public action API used by clients.
// Each player puts in the maximum legal amount where possible, which gives us
// all-in and side-pot coverage without bypassing turn or wager validation.
void completeHandWithMaximumLegalAction(Table& table, Chips expectedTotal) {
    for (std::size_t actions = 0; actions < 100; ++actions) {
        const auto publicView = table.viewFor();
        if (publicView.street == Street::showdown) return;
        assertChipInvariant(publicView, expectedTotal);
        assert(publicView.actingSeat);
        const auto actor = std::find_if(publicView.players.begin(), publicView.players.end(),
            [seat = *publicView.actingSeat](const TablePlayerView& player) { return player.seat == seat; });
        assert(actor != publicView.players.end());

        const auto privateView = table.viewFor(actor->name);
        assert(privateView.legalActions);
        const auto& legal = *privateView.legalActions;
        if (legal.raise) {
            table.submitAction(actor->name, Action::raise, legal.maximumAmount, privateView.eventSequence);
        } else if (legal.bet) {
            table.submitAction(actor->name, Action::bet, legal.maximumAmount, privateView.eventSequence);
        } else if (legal.call) {
            table.submitAction(actor->name, Action::call, 0, privateView.eventSequence);
        } else if (legal.check) {
            table.submitAction(actor->name, Action::check, 0, privateView.eventSequence);
        } else {
            assert(legal.fold);
            table.submitAction(actor->name, Action::fold, 0, privateView.eventSequence);
        }
    }
    assert(false && "a legal poker hand must finish in a bounded number of actions");
}

void assertHandPreservesChips(Table& table, Chips expectedTotal) {
    completeHandWithMaximumLegalAction(table, expectedTotal);
    const auto completed = table.viewFor();
    assert(completed.street == Street::showdown);
    assert(stackTotal(completed) == expectedTotal);
}

void completeHandByFolding(Table& table, Chips expectedTotal) {
    for (std::size_t actions = 0; actions < 100; ++actions) {
        const auto publicView = table.viewFor();
        if (publicView.street == Street::showdown) return;
        assertChipInvariant(publicView, expectedTotal);
        assert(publicView.actingSeat);
        const auto actor = std::find_if(publicView.players.begin(), publicView.players.end(),
            [seat = *publicView.actingSeat](const TablePlayerView& player) { return player.seat == seat; });
        assert(actor != publicView.players.end());
        const auto privateView = table.viewFor(actor->name);
        assert(privateView.legalActions && privateView.legalActions->fold);
        table.submitAction(actor->name, Action::fold, 0, privateView.eventSequence);
    }
    assert(false && "folding all but one player must finish the hand");
}

void completeHandPassively(Table& table, Chips expectedTotal) {
    for (std::size_t actions = 0; actions < 100; ++actions) {
        const auto publicView = table.viewFor();
        if (publicView.street == Street::showdown) return;
        assertChipInvariant(publicView, expectedTotal);
        assert(publicView.actingSeat);
        const auto actor = std::find_if(publicView.players.begin(), publicView.players.end(),
            [seat = *publicView.actingSeat](const TablePlayerView& player) { return player.seat == seat; });
        assert(actor != publicView.players.end());
        const auto privateView = table.viewFor(actor->name);
        assert(privateView.legalActions);
        const auto& legal = *privateView.legalActions;
        if (legal.check) table.submitAction(actor->name, Action::check, 0, privateView.eventSequence);
        else {
            assert(legal.call);
            table.submitAction(actor->name, Action::call, 0, privateView.eventSequence);
        }
    }
    assert(false && "a passive hand must finish in a bounded number of actions");
}

} // namespace

int main() {
    constexpr Chips tournamentTotal = 7 * 7'000;

    // Consecutive fold wins exercise hand reset, dealer movement, blind posts,
    // uncalled-contribution returns, and the total after every completed hand.
    Table repeatedHands("Repeated hands", 8, 7'000);
    for (std::size_t seat = 1; seat <= 7; ++seat) {
        repeatedHands.seatPlayer("Fold" + std::to_string(seat), PlayerKind::api, seat, 7'000);
    }
    repeatedHands.startHand();
    for (std::size_t hand = 0; hand < 20; ++hand) {
        completeHandByFolding(repeatedHands, tournamentTotal);
        assert(stackTotal(repeatedHands.viewFor()) == tournamentTotal);
        if (hand + 1 < 20) repeatedHands.startNextHand();
    }

    // Passive calls and checks play every street to showdown.  This covers
    // commitment reset between streets as well as pot settlement over a run
    // of hands that crosses a blind-level increase.
    Table passiveHands("Passive hands", 8, 7'000);
    for (std::size_t seat = 1; seat <= 7; ++seat) {
        passiveHands.seatPlayer("Call" + std::to_string(seat), PlayerKind::api, seat, 7'000);
    }
    passiveHands.startHand();
    for (std::size_t hand = 0; hand < 20; ++hand) {
        completeHandPassively(passiveHands, tournamentTotal);
        assert(stackTotal(passiveHands.viewFor()) == tournamentTotal);
        if (hand + 1 < 20) passiveHands.startNextHand();
    }

    // This is the client game's seven-player starting field.  Every completed
    // hand must retain all 49,000 chips, including an eventual sole winner.
    Table tournament("Seven players", 8, 7'000);
    for (std::size_t seat = 1; seat <= 7; ++seat) {
        tournament.seatPlayer("Player" + std::to_string(seat), PlayerKind::api, seat, 7'000);
    }
    tournament.startHand();
    for (std::size_t hand = 0; hand < 16; ++hand) {
        assertHandPreservesChips(tournament, tournamentTotal);
        const auto completed = tournament.viewFor();
        if (playersWithChips(completed) == 1) {
            const auto winner = std::find_if(completed.players.begin(), completed.players.end(),
                [](const TablePlayerView& player) { return player.stack > 0; });
            assert(winner != completed.players.end());
            assert(winner->stack == tournamentTotal);
            break;
        }
        tournament.startNextHand();
    }

    // Unequal stacks make the same maximum-action path create main and side
    // pots.  Their settlement must retain the full original stack total too.
    Table sidePots("Side pots", 4, 2'000);
    sidePots.seatPlayer("Short", PlayerKind::api, 1, 500);
    sidePots.seatPlayer("Medium", PlayerKind::api, 2, 1'000);
    sidePots.seatPlayer("Deep", PlayerKind::api, 3, 1'500);
    sidePots.seatPlayer("Covering", PlayerKind::api, 4, 2'000);
    sidePots.startHand();
    assertHandPreservesChips(sidePots, 5'000);

    // A folded player's smaller contribution remains in the main pot even when
    // the only all-in player is committed to a higher amount.
    Table foldedContribution("Folded contribution", 3, 2'000);
    foldedContribution.seatPlayer("Covering", PlayerKind::api, 1, 2'000);
    foldedContribution.seatPlayer("Folded", PlayerKind::api, 2, 2'000);
    foldedContribution.seatPlayer("AllIn", PlayerKind::api, 3, 1'300);
    foldedContribution.startHand();
    auto contributionView = foldedContribution.viewFor("Covering");
    foldedContribution.submitAction("Covering", Action::raise, 800, contributionView.eventSequence);
    contributionView = foldedContribution.viewFor("Folded");
    foldedContribution.submitAction("Folded", Action::call, 0, contributionView.eventSequence);
    contributionView = foldedContribution.viewFor("AllIn");
    foldedContribution.submitAction("AllIn", Action::raise, 1'300, contributionView.eventSequence);
    contributionView = foldedContribution.viewFor("Covering");
    foldedContribution.submitAction("Covering", Action::call, 0, contributionView.eventSequence);
    contributionView = foldedContribution.viewFor("Folded");
    foldedContribution.submitAction("Folded", Action::fold, 0, contributionView.eventSequence);
    completeHandPassively(foldedContribution, 5'300);
    const auto foldedContributionView = foldedContribution.viewFor();
    assert(foldedContributionView.pots.size() == 1);
    assert(foldedContributionView.pots.front().amount == 3'400);
    assert(stackTotal(foldedContributionView) == 5'300);
}
