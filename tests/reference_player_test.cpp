#include "bluffskill/poker/house.hpp"
#include "bluffskill/poker/reference_player.hpp"

#include <cassert>

namespace {

bluffskill::poker::TableView privateTurn(std::string name, bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    TableView view;
    view.street = Street::preflop;
    view.players.push_back({.name = std::move(name), .seat = 1, .stack = 1000, .acting = true, .holeCards = {first, second}});
    view.legalActions = LegalActions{.call = true, .raise = true, .fold = true, .callAmount = 100, .minimumAmount = 200, .maximumAmount = 1000};
    return view;
}

} // namespace

int main() {
    using namespace bluffskill;
    using namespace bluffskill::cards;
    using namespace bluffskill::poker;

    const auto aces = privateTurn("Bold", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 stableRandom{7};
    ReferencePlayer bold{"Bold", {.riskTolerance = 1.0, .optimism = 1.0, .variability = 0.0}};
    const auto boldDecision = bold.chooseResponse(aces, stableRandom);
    assert(boldDecision.action == Action::raise);
    assert(boldDecision.amount >= 200 && boldDecision.amount <= 1000);

    std::mt19937_64 cautiousRandom{7};
    ReferencePlayer cautious{"Bold", {.riskTolerance = 0.0, .optimism = 0.0, .variability = 0.0}};
    const auto cautiousDecision = cautious.chooseResponse(aces, cautiousRandom);
    assert(cautiousDecision.action == Action::call);

    try {
        ReferencePlayer invalid{"Invalid", {.riskTolerance = -0.01, .optimism = 0.5, .variability = 0.5}};
        assert(false && "invalid profiles must be rejected");
    } catch (const std::invalid_argument&) {
    }

    House house;
    const auto competition = house.createSingleTableTournament({.maximumPlayers = 3, .startingStack = 7000});
    const auto populated = house.createReferencePlayers(competition.name, 2);
    assert(populated.tables.front().players.size() == 2);
    const auto seated = house.createApiPlayer(competition.name, "Red", "Human");
    assert(seated.tables.front().players.size() == 3);
    auto view = house.tableView(competition.name, "Red", "Human");
    assert(!view.actionHistory.empty());
    assert(!view.actingSeat || *view.actingSeat == 3);

    if (view.legalActions) {
        const auto& legal = *view.legalActions;
        const auto action = legal.check ? Action::check : legal.call ? Action::call : Action::fold;
        house.submitAction(competition.name, "Red", "Human", action, 0, view.eventSequence);
        view = house.tableView(competition.name, "Red", "Human");
        assert(!view.actingSeat || *view.actingSeat == 3);
    }
}
