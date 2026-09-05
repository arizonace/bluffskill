#pragma once

#include "bluffskill/poker/player.hpp"
#include "bluffskill/poker/reference_player.hpp"
#include "bluffskill/poker/table.hpp"

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
    std::size_t maximumPlayers{10};
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
    ChipDenominations chipDenominations;
    std::vector<TableSummary> tables;
};

class House {
public:
    // `House` is the one singleton per server process. The application owns it.
    explicit House(BlindSchedule blindSchedule = {}, ChipDenominations chipDenominations = defaultChipDenominations());
    [[nodiscard]] CompetitionSummary createSingleTableTournament(TournamentSpec spec);
    [[nodiscard]] CompetitionSummary createReferencePlayers(
        std::string_view competitionName, std::size_t count, ReferencePlayerType type = ReferencePlayerType::leo);
    [[nodiscard]] CompetitionSummary createApiPlayer(std::string_view competitionName, std::string_view tableName, std::string name);
    [[nodiscard]] std::vector<CompetitionSummary> competitions() const;
    [[nodiscard]] std::optional<CompetitionSummary> competition(std::string_view name) const;
    [[nodiscard]] TableView tableView(std::string_view competitionName, std::string_view tableName, std::string_view viewerName = {}) const;
    // Server-console inspection only. REST adapters must never project bot profiles.
    [[nodiscard]] std::optional<ReferencePlayerInspection> referencePlayerInspection(
        std::string_view competitionName, std::string_view tableName, std::string_view playerName) const;
    void submitAction(std::string_view competitionName, std::string_view tableName, std::string_view playerName,
        Action action, Chips amount, std::uint64_t expectedSequence);
    void startNextHand(std::string_view competitionName, std::string_view tableName);
    void restartTable(std::string_view competitionName, std::string_view tableName);
    void setBlindSchedule(BlindSchedule blindSchedule);

private:
    struct Competition {
        CompetitionSummary summary;
        struct SeatedPlayer {
            std::unique_ptr<Player> player;
            std::size_t tableIndex{0};
            std::size_t seat{0};
        };
        std::vector<SeatedPlayer> players;
        std::vector<std::unique_ptr<Table>> tables;
    };

    [[nodiscard]] Competition& findCompetition(std::string_view name);
    [[nodiscard]] const Competition* findCompetition(std::string_view name) const;
    [[nodiscard]] CompetitionSummary summaryOf(const Competition& competition) const;
    [[nodiscard]] std::string nextCompetitionName() const;
    [[nodiscard]] std::string nextReferencePlayerName(const Competition& competition);
    [[nodiscard]] std::size_t findTableIndex(const Competition& competition, std::string_view tableName) const;
    [[nodiscard]] Table& findTable(Competition& competition, std::string_view tableName);
    [[nodiscard]] const Table& findTable(const Competition& competition, std::string_view tableName) const;
    [[nodiscard]] std::optional<std::size_t> firstFreeSeat(const Competition& competition, std::size_t tableIndex) const;
    void startTableIfReady(Competition& competition, std::size_t tableIndex);
    void validatePlayerName(const Competition& competition, std::string_view name) const;
    void advanceReferencePlayers(Competition& competition, std::size_t tableIndex);

    std::mt19937_64 random_{std::random_device{}()};
    BlindSchedule blindSchedule_;
    ChipDenominations chipDenominations_;
    std::vector<Competition> competitions_;
};

} // namespace bluffskill::poker
