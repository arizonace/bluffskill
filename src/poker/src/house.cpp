#include "bluffskill/poker/house.hpp"

#include <array>
#include <stdexcept>

namespace bluffskill::poker {

std::string House::nextCompetitionName() const {
    constexpr std::array places{"Valhalla", "Toronto", "Westeros", "Pantheon", "Riverlands", "Solaris", "Mordor", "NorthPole"};
    const auto index = competitions_.size();
    if (index < places.size()) return places[index];
    return std::string{places[index % places.size()]} + "-" + std::to_string(index + 1);
}

CompetitionSummary House::createSingleTableTournament(TournamentSpec spec) {
    if (spec.maximumPlayers == 0 || spec.maximumPlayers > 8) {
        throw std::invalid_argument("maximumPlayers must be between 1 and 8");
    }
    if (spec.startingStack == 0) throw std::invalid_argument("startingStack must be positive");

    CompetitionSummary competition{
        .name = nextCompetitionName(),
        .style = CompetitionStyle::tournament,
        .tournament = spec,
        .tables = {{.name = "Red", .seats = 0}},
    };
    competitions_.push_back(competition);
    return competition;
}

const std::vector<CompetitionSummary>& House::competitions() const noexcept { return competitions_; }

} // namespace bluffskill::poker
