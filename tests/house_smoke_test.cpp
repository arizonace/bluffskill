#include "bluffskill/poker/house.hpp"

#include <cassert>
#include <set>

int main() {
    bluffskill::poker::House house;
    const auto created = house.createSingleTableTournament({.maximumPlayers = 8, .startingStack = 7000});
    const auto populated = house.createReferencePlayers(created.name, 6);
    assert(populated.players.size() == 6);
    assert(populated.tables.front().seats == 6);
    std::set<std::string> names;
    for (const auto& player : populated.players) {
        assert(player.kind == bluffskill::poker::PlayerKind::reference);
        names.insert(player.name);
    }
    assert(names.size() == 6);
}
