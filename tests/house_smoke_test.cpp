#include "bluffskill/poker/house.hpp"

#include <cassert>
#include <set>

int main() {
    bluffskill::poker::House house;
    const auto created = house.createSingleTableTournament({.maximumPlayers = 8, .startingStack = 7000});
    const auto populated = house.createReferencePlayers(created.name, 6);
    assert(populated.tables.front().players.size() == 6);
    std::set<std::string> names;
    std::set<std::size_t> seats;
    for (const auto& seatedPlayer : populated.tables.front().players) {
        assert(seatedPlayer.player.kind == bluffskill::poker::PlayerKind::reference);
        names.insert(seatedPlayer.player.name);
        seats.insert(seatedPlayer.seat);
    }
    assert(names.size() == 6);
    assert(seats == std::set<std::size_t>({1, 2, 3, 4, 5, 6}));

    const auto withApiPlayer = house.createApiPlayer(created.name, "Red", "Arizona");
    const auto& seatedApiPlayer = withApiPlayer.tables.front().players.back();
    assert(seatedApiPlayer.player.name == "Arizona");
    assert(seatedApiPlayer.player.kind == bluffskill::poker::PlayerKind::api);
    assert(seatedApiPlayer.seat == 7);

    bluffskill::poker::House customChipsHouse{{}, {50, 200, 1'000}};
    const auto customChipsCompetition = customChipsHouse.createSingleTableTournament({.maximumPlayers = 2, .startingStack = 7'000});
    assert((customChipsCompetition.chipDenominations == bluffskill::poker::ChipDenominations{50, 200, 1'000}));
}
