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
    std::size_t maximumSeats{0};
    struct SeatedPlayer {
        PlayerSummary player;
        std::size_t seat{0}; // One-based table seat number.
    };
    std::vector<SeatedPlayer> players;
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
    [[nodiscard]] CompetitionSummary createReferencePlayers(std::string_view competitionName, std::size_t count);
    [[nodiscard]] CompetitionSummary createApiPlayer(std::string_view competitionName, std::string_view tableName, std::string name);
    [[nodiscard]] std::vector<CompetitionSummary> competitions() const;
    [[nodiscard]] std::optional<CompetitionSummary> competition(std::string_view name) const;

private:
    struct Competition {
        CompetitionSummary summary;
        struct SeatedPlayer {
            std::unique_ptr<Player> player;
            std::size_t tableIndex{0};
            std::size_t seat{0};
        };
        std::vector<SeatedPlayer> players;
    };

    [[nodiscard]] Competition& findCompetition(std::string_view name);
    [[nodiscard]] const Competition* findCompetition(std::string_view name) const;
    [[nodiscard]] CompetitionSummary summaryOf(const Competition& competition) const;
    [[nodiscard]] std::string nextCompetitionName() const;
    [[nodiscard]] std::string nextReferencePlayerName(const Competition& competition);
    [[nodiscard]] std::size_t findTableIndex(const Competition& competition, std::string_view tableName) const;
    [[nodiscard]] std::optional<std::size_t> firstFreeSeat(const Competition& competition, std::size_t tableIndex) const;
    void validatePlayerName(const Competition& competition, std::string_view name) const;

    std::mt19937_64 random_{std::random_device{}()};
    std::vector<Competition> competitions_;
};

} // namespace bluffskill::poker
