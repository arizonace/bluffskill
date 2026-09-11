#include "bluffskill/poker/house.hpp"
#include "bluffskill/poker/august_leo_reference_player.hpp"
#include "bluffskill/poker/august_virgo_reference_player.hpp"
#include "bluffskill/poker/leo_reference_player.hpp"
#include "bluffskill/poker/virgo_reference_player.hpp"

#include <cassert>

namespace {

bluffskill::poker::TableView privateTurn(std::string name, bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    TableView view;
    view.street = Street::preflop;
    view.chipDenominations = {25, 100, 500, 1000};
    view.players.push_back({.name = std::move(name), .seat = 1, .stack = 1000, .acting = true, .holeCards = {first, second}});
    view.legalActions = LegalActions{.call = true, .raise = true, .fold = true, .callAmount = 100, .minimumAmount = 200, .maximumAmount = 1000};
    return view;
}

bluffskill::poker::TableView checkedFlop(std::string name, bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    auto view = privateTurn(std::move(name), first, second);
    view.street = Street::flop;
    view.communityCards = {{bluffskill::cards::Suit::clubs, bluffskill::cards::Rank::ace},
        {bluffskill::cards::Suit::diamonds, bluffskill::cards::Rank::king},
        {bluffskill::cards::Suit::hearts, bluffskill::cards::Rank::two}};
    view.pots = {{.amount = 300, .eligibleSeats = {1, 2}}};
    view.legalActions = LegalActions{.check = true, .bet = true, .fold = true, .minimumAmount = 50, .maximumAmount = 1000};
    return view;
}

} // namespace

int main() {
    using namespace bluffskill;
    using namespace bluffskill::cards;
    using namespace bluffskill::poker;

    const auto aces = privateTurn("Bold", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 stableRandom{7};
    // The archived August policy remains deterministic for its established cases.
    AugustLeoReferencePlayer bold{"Bold", {.riskTolerance = 1.0, .optimism = 1.0, .variability = 0.0}};
    assert(bold.referenceType() == ReferencePlayerType::augustLeo);
    const auto boldDecision = bold.chooseResponse(aces, stableRandom);
    assert(boldDecision.action == Action::raise);
    assert(boldDecision.amount >= 200 && boldDecision.amount <= 1000);
    assert(boldDecision.amount % 25 == 0);

    std::mt19937_64 cautiousRandom{7};
    AugustLeoReferencePlayer cautious{"Bold", {.riskTolerance = 0.0, .optimism = 0.0, .variability = 0.0}};
    const auto cautiousDecision = cautious.chooseResponse(aces, cautiousRandom);
    assert(cautiousDecision.action == Action::call);

    try {
        AugustLeoReferencePlayer invalid{"Invalid", {.riskTolerance = -0.01, .optimism = 0.5, .variability = 0.5}};
        assert(false && "invalid profiles must be rejected");
    } catch (const std::invalid_argument&) {
    }

    AugustVirgoReferencePlayer virgo{"Virgo", {.curiosity = 1.0, .hope = 1.0, .empathy = 0.5, .longevity = 0.5}};
    assert(virgo.referenceType() == ReferencePlayerType::augustVirgo);
    const auto virgoView = privateTurn("Virgo", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 virgoRandom{7};
    const auto virgoDecision = virgo.chooseResponse(virgoView, virgoRandom);
    assert(virgoDecision.action == Action::call || virgoDecision.action == Action::raise || virgoDecision.action == Action::fold);
    if (virgoDecision.action == Action::raise) assert(virgoDecision.amount >= 200 && virgoDecision.amount <= 1000 && virgoDecision.amount % 25 == 0);
    assert(virgo.referenceType() == ReferencePlayerType::augustVirgo);
    assert(virgo.parameters().size() == 4);

    auto legacyStrongFlop = checkedFlop("Virgo", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    const auto legacyStrongDecision = virgo.chooseResponse(legacyStrongFlop, virgoRandom);
    assert(legacyStrongDecision.action == Action::check);

    try {
        AugustVirgoReferencePlayer invalid{"Invalid", {.curiosity = 0.5, .hope = 0.5, .empathy = 1.1, .longevity = 0.5}};
        assert(false && "invalid Virgo profiles must be rejected");
    } catch (const std::invalid_argument&) {
    }

    // The new defaults retain their archetypes while taking initiative with
    // premium holdings instead of relying on the legacy fixed-stack behavior.
    LeoReferencePlayer improvedLeo{"Bold", {.riskTolerance = 0.5, .optimism = 0.5, .variability = 0.0}};
    std::mt19937_64 improvedLeoRandom{7};
    const auto improvedLeoDecision = improvedLeo.chooseResponse(aces, improvedLeoRandom);
    assert(improvedLeoDecision.action == Action::raise);
    assert(improvedLeoDecision.amount >= 200 && improvedLeoDecision.amount <= 1000);

    VirgoReferencePlayer improvedVirgo{"Virgo", {.curiosity = 0.5, .hope = 0.7, .empathy = 0.5, .longevity = 0.5}};
    auto strongFlop = checkedFlop("Virgo", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 improvedVirgoRandom{7};
    const auto improvedVirgoDecision = improvedVirgo.chooseResponse(strongFlop, improvedVirgoRandom);
    assert(improvedVirgoDecision.action == Action::bet);
    assert(improvedVirgoDecision.amount >= 50 && improvedVirgoDecision.amount <= 1000);

    House house;
    const auto competition = house.createSingleTableTournament({.maximumPlayers = 3, .startingStack = 7000});
    const auto populated = house.createReferencePlayers(competition.name, 2);
    assert(populated.tables.front().players.size() == 2);
    assert(populated.tables.front().players.front().player.referenceType == ReferencePlayerType::leo);
    const auto mixed = house.createReferencePlayers(competition.name, 1, ReferencePlayerType::virgo);
    assert(mixed.tables.front().players.back().player.referenceType == ReferencePlayerType::virgo);
    const auto virgoInspection = house.referencePlayerInspection(competition.name, "Red", mixed.tables.front().players.back().player.name);
    assert(virgoInspection && virgoInspection->type == ReferencePlayerType::virgo);
    assert(virgoInspection->parameters.size() == 4);
    assert(mixed.tables.front().players.size() == 3);
    // This separate table retains the original two-reference-player flow.
    House activeHouse;
    const auto activeCompetition = activeHouse.createSingleTableTournament({.maximumPlayers = 3, .startingStack = 7000});
    [[maybe_unused]] const auto activeReferences = activeHouse.createReferencePlayers(activeCompetition.name, 2);
    const auto seated = activeHouse.createApiPlayer(activeCompetition.name, "Red", "Human");
    assert(seated.tables.front().players.size() == 3);
    auto view = activeHouse.tableView(activeCompetition.name, "Red", "Human");
    assert(!view.actionHistory.empty());
    assert(!view.actingSeat || *view.actingSeat == 3);

    if (view.legalActions) {
        const auto& legal = *view.legalActions;
        const auto action = legal.check ? Action::check : legal.call ? Action::call : Action::fold;
        activeHouse.submitAction(activeCompetition.name, "Red", "Human", action, 0, view.eventSequence);
        view = activeHouse.tableView(activeCompetition.name, "Red", "Human");
        assert(!view.actingSeat || *view.actingSeat == 3);
    }
}
