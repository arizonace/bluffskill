#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bluffskill::poker {

enum class CompetitionStyle { tournament, cash };

struct TournamentSpec {
    std::size_t maximumPlayers{8};
    unsigned int startingStack{7000};
};

struct TableSummary {
    std::string name;
    std::size_t seats{0};
};

struct CompetitionSummary {
    std::string name;
    CompetitionStyle style{CompetitionStyle::tournament};
    TournamentSpec tournament;
    std::vector<TableSummary> tables;
};

class House {
public:
    // `House` is the one singleton per server process. The application owns it.
    [[nodiscard]] CompetitionSummary createSingleTableTournament(TournamentSpec spec);
    [[nodiscard]] const std::vector<CompetitionSummary>& competitions() const noexcept;

private:
    [[nodiscard]] std::string nextCompetitionName() const;
    std::vector<CompetitionSummary> competitions_;
};

} // namespace bluffskill::poker
