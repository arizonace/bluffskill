#include "bluffskill/poker/house.hpp"

#include <cassert>
#include <cctype>
#include <set>
#include <vector>

int main() {
    bluffskill::poker::House house;
    assert((house.referencePlayerTypes() == std::vector<bluffskill::poker::ReferencePlayerType>{
        bluffskill::poker::ReferencePlayerType::leo,
        bluffskill::poker::ReferencePlayerType::augustLeo,
        bluffskill::poker::ReferencePlayerType::virgo,
        bluffskill::poker::ReferencePlayerType::augustVirgo,
        bluffskill::poker::ReferencePlayerType::libra,
    }));
    const auto created = house.createSingleTableTournament({.maximumPlayers = 10, .startingStack = 7000});
    assert(created.tables.front().name == "Hydrogen");
    assert(created.tables.front().gameName == "Red");
    const auto populated = house.createReferencePlayers(created.name, 6);
    assert(populated.tables.front().players.size() == 6);
    std::set<std::string> names;
    std::set<std::size_t> seats;
    for (const auto& seatedPlayer : populated.tables.front().players) {
        assert(seatedPlayer.player.kind == bluffskill::poker::PlayerKind::reference);
        assert(seatedPlayer.player.name.size() > 3);
        for (const auto character : seatedPlayer.player.name.substr(seatedPlayer.player.name.size() - 3)) {
            assert(std::isdigit(static_cast<unsigned char>(character)));
        }
        names.insert(seatedPlayer.player.name);
        seats.insert(seatedPlayer.seat);
    }
    assert(names.size() == 6);
    assert(seats == std::set<std::size_t>({1, 2, 3, 4, 5, 6}));
    const auto inspection = house.referencePlayerInspection(created.name, "Hydrogen", *names.begin());
    assert(inspection && inspection->type == bluffskill::poker::ReferencePlayerType::leo);
    assert(inspection->parameters.size() == 3);
    for (const auto& parameter : inspection->parameters) assert(parameter.value >= 0.15 && parameter.value <= 0.85);

    const auto withApiPlayer = house.createApiPlayer(created.name, "Hydrogen", "Arizona");
    const auto& seatedApiPlayer = withApiPlayer.tables.front().players.back();
    assert(seatedApiPlayer.player.name == "Arizona");
    assert(seatedApiPlayer.player.kind == bluffskill::poker::PlayerKind::api);
    assert(seatedApiPlayer.seat == 7);
    assert(!house.referencePlayerInspection(created.name, "Hydrogen", "Arizona"));
    assert(!house.recordsHoleCardsWhenFolding(created.name, "Hydrogen", "Arizona"));
    assert(house.recordsHoleCardsWhenFolding(created.name, "Hydrogen", *names.begin()));

    bluffskill::poker::House optedInHouse;
    const auto optedInCompetition = optedInHouse.createSingleTableTournament({.maximumPlayers = 2, .startingStack = 7'000});
    [[maybe_unused]] const auto optedIn = optedInHouse.createApiPlayer(
        optedInCompetition.name, "Hydrogen", "CardRecorder", true);
    assert(optedInHouse.recordsHoleCardsWhenFolding(optedInCompetition.name, "Hydrogen", "CardRecorder"));
    optedInHouse.pauseTable(optedInCompetition.name, "Hydrogen");
    optedInHouse.resumeTable(optedInCompetition.name, "Hydrogen");

    bluffskill::poker::House mixedHouse;
    const auto mixedCompetition = mixedHouse.createSingleTableTournament({.maximumPlayers = 10, .startingStack = 7'000});
    [[maybe_unused]] const auto leo = mixedHouse.createReferencePlayers(mixedCompetition.name, 4, bluffskill::poker::ReferencePlayerType::leo);
    const auto mixed = mixedHouse.createReferencePlayers(mixedCompetition.name, 5, bluffskill::poker::ReferencePlayerType::virgo);
    std::set<std::string> mixedNames;
    for (std::size_t index = 0; index < 4; ++index) {
        assert(mixed.tables.front().players[index].player.referenceType == bluffskill::poker::ReferencePlayerType::leo);
        mixedNames.insert(mixed.tables.front().players[index].player.name);
    }
    for (std::size_t index = 4; index < 9; ++index) {
        assert(mixed.tables.front().players[index].player.referenceType == bluffskill::poker::ReferencePlayerType::virgo);
        mixedNames.insert(mixed.tables.front().players[index].player.name);
    }
    assert(mixedNames.size() == 9);

    bluffskill::poker::House allTypesHouse;
    const auto allTypesCompetition = allTypesHouse.createSingleTableTournament({.maximumPlayers = 5, .startingStack = 7'000});
    for (const auto type : allTypesHouse.referencePlayerTypes()) {
        [[maybe_unused]] const auto populatedType = allTypesHouse.createReferencePlayers(allTypesCompetition.name, 1, type);
    }
    const auto allTypes = allTypesHouse.competition(allTypesCompetition.name);
    assert(allTypes && allTypes->tables.front().players.size() == 5);
    for (std::size_t index = 0; index < allTypes->tables.front().players.size(); ++index) {
        const auto& player = allTypes->tables.front().players[index].player;
        assert(player.referenceType == allTypesHouse.referencePlayerTypes()[index]);
        const auto inspection = allTypesHouse.referencePlayerInspection(allTypesCompetition.name, "Hydrogen", player.name);
        assert(inspection && inspection->type == *player.referenceType);
    }

    bluffskill::poker::House stagedHouse;
    const auto staged = stagedHouse.createSingleTableTournament({.maximumPlayers = 10, .startingStack = 7'000});
    [[maybe_unused]] const auto stagedReferences = stagedHouse.createReferencePlayers(staged.name, 3);
    const auto stagedWithHuman = stagedHouse.createApiPlayer(staged.name, "Hydrogen", "Human");
    assert(stagedWithHuman.tables.front().players.back().seat == 4);
    assert(stagedHouse.tableView(staged.name, "Hydrogen", "Human").street == bluffskill::poker::Street::waiting);
    [[maybe_unused]] const auto stagedFinalReferences = stagedHouse.createReferencePlayers(staged.name, 6);
    const auto fullTable = stagedHouse.tableView(staged.name, "Hydrogen", "Human");
    assert(fullTable.players.size() == 10);
    assert(fullTable.street != bluffskill::poker::Street::waiting);

    bluffskill::poker::House customChipsHouse{{}, {50, 200, 1'000}};
    const auto customChipsCompetition = customChipsHouse.createSingleTableTournament({.maximumPlayers = 2, .startingStack = 7'000});
    assert((customChipsCompetition.chipDenominations == bluffskill::poker::ChipDenominations{50, 200, 1'000}));

    bluffskill::poker::House futureSettingsHouse;
    futureSettingsHouse.setDefaultCompetitionSettings({.smallBlind = 50}, {50, 200, 1'000});
    const auto futureSettingsCompetition = futureSettingsHouse.createSingleTableTournament({.maximumPlayers = 2, .startingStack = 15'000});
    const auto futureSettingsView = futureSettingsHouse.tableView(futureSettingsCompetition.name, "Hydrogen");
    assert(futureSettingsView.smallBlind == 50);
    assert(futureSettingsView.startingStack == 15'000);
    assert((futureSettingsView.chipDenominations == bluffskill::poker::ChipDenominations{50, 200, 1'000}));

    bluffskill::poker::House clearableHouse;
    const auto clearableCompetition = clearableHouse.createSingleTableTournament({.maximumPlayers = 3, .startingStack = 7'000});
    [[maybe_unused]] const auto firstPlayer = clearableHouse.createApiPlayer(clearableCompetition.name, "Hydrogen", "One");
    [[maybe_unused]] const auto secondPlayer = clearableHouse.createApiPlayer(clearableCompetition.name, "Hydrogen", "Two");
    [[maybe_unused]] const auto thirdPlayer = clearableHouse.createApiPlayer(clearableCompetition.name, "Hydrogen", "Three");
    assert(clearableHouse.hasGamesInProgress());
    try {
        clearableHouse.clear();
        assert(false && "an active house must not be cleared");
    } catch (const std::logic_error&) {
    }
    const auto quitWinner = clearableHouse.quitGame(clearableCompetition.name, "Hydrogen");
    assert(quitWinner.player == "One");
    assert(quitWinner.seat == 1);
    assert(quitWinner.chips == 7'000);
    assert(!clearableHouse.hasGamesInProgress());
    clearableHouse.clear();
    assert(clearableHouse.competitions().empty());

    bluffskill::poker::House emptyHouse;
    [[maybe_unused]] const auto emptyCompetition = emptyHouse.createSingleTableTournament({.maximumPlayers = 3, .startingStack = 7'000});
    assert(!emptyHouse.hasGamesInProgress());
    emptyHouse.clear();
    assert(emptyHouse.competitions().empty());

    bluffskill::poker::House gameNameHouse;
    const auto namedCompetition = gameNameHouse.createSingleTableTournament({.maximumPlayers = 2, .startingStack = 7'000});
    [[maybe_unused]] const auto namedOne = gameNameHouse.createApiPlayer(namedCompetition.name, "Hydrogen", "First");
    [[maybe_unused]] const auto namedTwo = gameNameHouse.createApiPlayer(namedCompetition.name, "Hydrogen", "Second");
    for (int restart = 0; restart < 99; ++restart) gameNameHouse.restartTable(namedCompetition.name, "Hydrogen");
    assert(gameNameHouse.tableView(namedCompetition.name, "Hydrogen").gameName == "IndianRed");
    try {
        gameNameHouse.restartTable(namedCompetition.name, "Hydrogen");
        assert(false && "a competition must not create a 101st game");
    } catch (const std::runtime_error&) {
    }
}
