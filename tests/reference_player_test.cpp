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

bluffskill::poker::TableView headsUpPreflop(std::string name, bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    auto view = privateTurn(std::move(name), first, second);
    view.smallBlind = 50;
    view.bigBlind = 100;
    view.smallBlindSeat = 1;
    view.bigBlindSeat = 2;
    view.players.front().dealer = true;
    view.players.emplace_back(TablePlayerView{.name = "Opponent", .seat = 2, .stack = 1000});
    return view;
}

bluffskill::poker::TableView fullRingPreflop(std::string name, bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    auto view = headsUpPreflop(std::move(name), first, second);
    for (std::size_t seat = 3; seat <= 10; ++seat) {
        view.players.emplace_back(TablePlayerView{.name = "Opponent " + std::to_string(seat), .seat = seat, .stack = 1000});
    }
    return view;
}

bluffskill::poker::TableView headsUpCheckedStreet(std::string name, bluffskill::poker::Street street,
    bluffskill::cards::Card first, bluffskill::cards::Card second) {
    using namespace bluffskill::poker;
    auto view = headsUpPreflop(std::move(name), first, second);
    view.street = street;
    view.communityCards = {{bluffskill::cards::Suit::clubs, bluffskill::cards::Rank::ace},
        {bluffskill::cards::Suit::diamonds, bluffskill::cards::Rank::king},
        {bluffskill::cards::Suit::hearts, bluffskill::cards::Rank::two}};
    if (street == Street::turn || street == Street::river) {
        view.communityCards.push_back({bluffskill::cards::Suit::clubs, bluffskill::cards::Rank::nine});
    }
    if (street == Street::river) {
        view.communityCards.push_back({bluffskill::cards::Suit::diamonds, bluffskill::cards::Rank::four});
    }
    view.pots = {{.amount = street == Street::river ? 800 : 600, .eligibleSeats = {1, 2}}};
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

    // New Leo opens a materially wider heads-up range, while retaining a
    // disciplined full-ring fold with the same marginal suited connector.
    const auto suitedSevenFive = headsUpPreflop("Bold", {Suit::hearts, Rank::seven}, {Suit::hearts, Rank::five});
    std::mt19937_64 headsUpRandom{7};
    const auto headsUpDecision = improvedLeo.chooseResponse(suitedSevenFive, headsUpRandom);
    assert(headsUpDecision.action == Action::raise);
    assert(headsUpDecision.amount >= 200 && headsUpDecision.amount <= 1000);
    const auto fullRingSevenFive = fullRingPreflop("Bold", {Suit::hearts, Rank::seven}, {Suit::hearts, Rank::five});
    std::mt19937_64 fullRingRandom{7};
    assert(improvedLeo.chooseResponse(fullRingSevenFive, fullRingRandom).action == Action::fold);

    // A premium hand facing an open is re-raised from the wager already made,
    // rather than from an arbitrary fraction of Leo's remaining stack.
    auto threeBetSpot = headsUpPreflop("Bold", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    threeBetSpot.currentBet = 300;
    threeBetSpot.legalActions = LegalActions{.call = true, .raise = true, .fold = true, .callAmount = 300, .minimumAmount = 600, .maximumAmount = 1000};
    threeBetSpot.actionHistory.push_back({.player = "Opponent", .seat = 2, .street = Street::preflop, .action = Action::raise,
        .amount = 300, .stackAfter = 700, .potAfter = 450, .currentBetAfter = 300});
    std::mt19937_64 threeBetRandom{7};
    const auto threeBetDecision = improvedLeo.chooseResponse(threeBetSpot, threeBetRandom);
    assert(threeBetDecision.action == Action::raise);
    assert(threeBetDecision.amount >= 600 && threeBetDecision.amount <= 1000);

    // After a called flop bet, a weak turn is checked. A genuine made hand
    // continues, and a river value bet uses Leo's larger polar sizing band.
    auto weakTurn = headsUpCheckedStreet("Bold", Street::turn, {Suit::spades, Rank::seven}, {Suit::hearts, Rank::six});
    weakTurn.actionHistory = {{.player = "Bold", .seat = 1, .street = Street::flop, .action = Action::bet, .amount = 250},
        {.player = "Opponent", .seat = 2, .street = Street::flop, .action = Action::call, .amount = 250}};
    std::mt19937_64 weakTurnRandom{7};
    assert(improvedLeo.chooseResponse(weakTurn, weakTurnRandom).action == Action::check);

    auto strongTurn = headsUpCheckedStreet("Bold", Street::turn, {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    strongTurn.actionHistory = weakTurn.actionHistory;
    std::mt19937_64 strongTurnRandom{7};
    const auto strongTurnDecision = improvedLeo.chooseResponse(strongTurn, strongTurnRandom);
    assert(strongTurnDecision.action == Action::bet);
    assert(strongTurnDecision.amount >= 50 && strongTurnDecision.amount <= 1000);

    auto strongRiver = headsUpCheckedStreet("Bold", Street::river, {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    strongRiver.actionHistory = {{.player = "Bold", .seat = 1, .street = Street::turn, .action = Action::bet, .amount = 350},
        {.player = "Opponent", .seat = 2, .street = Street::turn, .action = Action::call, .amount = 350}};
    std::mt19937_64 strongRiverRandom{7};
    const auto strongRiverDecision = improvedLeo.chooseResponse(strongRiver, strongRiverRandom);
    assert(strongRiverDecision.action == Action::bet);
    assert(strongRiverDecision.amount >= 650 && strongRiverDecision.amount <= 1000);

    VirgoReferencePlayer improvedVirgo{"Virgo", {.curiosity = 0.5, .hope = 0.7, .empathy = 0.5, .longevity = 0.5}};
    auto strongFlop = checkedFlop("Virgo", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 improvedVirgoRandom{7};
    const auto improvedVirgoDecision = improvedVirgo.chooseResponse(strongFlop, improvedVirgoRandom);
    assert(improvedVirgoDecision.action == Action::bet);
    assert(improvedVirgoDecision.amount >= 50 && improvedVirgoDecision.amount <= 1000);

    // New Virgo is deliberately selective heads-up: it does not complete a
    // weak hand, but opens a premium hand with a controlled legal size.
    const auto virgoWeakHeadsUp = headsUpPreflop("Virgo", {Suit::spades, Rank::seven}, {Suit::hearts, Rank::two});
    std::mt19937_64 virgoWeakHeadsUpRandom{7};
    assert(improvedVirgo.chooseResponse(virgoWeakHeadsUp, virgoWeakHeadsUpRandom).action == Action::fold);
    const auto virgoPremiumHeadsUp = headsUpPreflop("Virgo", {Suit::spades, Rank::ace}, {Suit::hearts, Rank::ace});
    std::mt19937_64 virgoPremiumHeadsUpRandom{7};
    const auto virgoPremiumHeadsUpDecision = improvedVirgo.chooseResponse(virgoPremiumHeadsUp, virgoPremiumHeadsUpRandom);
    assert(virgoPremiumHeadsUpDecision.action == Action::raise);
    assert(virgoPremiumHeadsUpDecision.amount >= 200 && virgoPremiumHeadsUpDecision.amount <= 1000);

    // Heads-up top pair may use Virgo's compact value size on a safe board.
    const auto virgoThinValue = headsUpCheckedStreet("Virgo", Street::flop, {Suit::spades, Rank::ace}, {Suit::hearts, Rank::seven});
    std::mt19937_64 virgoThinValueRandom{7};
    const auto virgoThinValueDecision = improvedVirgo.chooseResponse(virgoThinValue, virgoThinValueRandom);
    assert(virgoThinValueDecision.action == Action::bet);
    assert(virgoThinValueDecision.amount >= 50 && virgoThinValueDecision.amount <= 300);

    // A large heads-up river bet does not receive a speculative call with a
    // one-pair hand.
    auto virgoLargeRiver = headsUpCheckedStreet("Virgo", Street::river, {Suit::spades, Rank::ace}, {Suit::hearts, Rank::seven});
    virgoLargeRiver.legalActions = LegalActions{.call = true, .fold = true, .callAmount = 600};
    std::mt19937_64 virgoLargeRiverRandom{7};
    assert(improvedVirgo.chooseResponse(virgoLargeRiver, virgoLargeRiverRandom).action == Action::fold);

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
