#pragma once

#include "bluffskill/poker/player.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
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
    std::vector<PlayerSummary> players;
};

class House {
public:
    // `House` is the one singleton per server process. The application owns it.
    [[nodiscard]] CompetitionSummary createSingleTableTournament(TournamentSpec spec);
    [[nodiscard]] CompetitionSummary createReferencePlayers(std::string_view competitionName, std::size_t count);
    [[nodiscard]] std::vector<CompetitionSummary> competitions() const;
    [[nodiscard]] std::optional<CompetitionSummary> competition(std::string_view name) const;

private:
    struct Competition {
        CompetitionSummary summary;
        std::vector<std::unique_ptr<Player>> players;
    };

    [[nodiscard]] Competition& findCompetition(std::string_view name);
    [[nodiscard]] const Competition* findCompetition(std::string_view name) const;
    [[nodiscard]] CompetitionSummary summaryOf(const Competition& competition) const;
    [[nodiscard]] std::string nextCompetitionName() const;
    [[nodiscard]] std::string nextReferencePlayerName(const Competition& competition);

    std::mt19937_64 random_{std::random_device{}()};
    std::vector<Competition> competitions_;
};

} // namespace bluffskill::poker
