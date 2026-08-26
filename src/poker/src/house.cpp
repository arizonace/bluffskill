#include "bluffskill/poker/house.hpp"
#include "bluffskill/poker/reference_player.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace bluffskill::poker {

std::string House::nextCompetitionName() const {
    constexpr std::array places{"Valhalla", "Toronto", "Westeros", "Pantheon", "Riverlands", "Solaris", "Mordor", "NorthPole"};
    const auto index = competitions_.size();
    if (index < places.size()) return places[index];
    return std::string{places[index % places.size()]} + "-" + std::to_string(index + 1);
}

namespace {

std::string normalizeName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return normalized;
}

} // namespace

CompetitionSummary House::createSingleTableTournament(TournamentSpec spec) {
    if (spec.maximumPlayers == 0 || spec.maximumPlayers > 8) {
        throw std::invalid_argument("maximumPlayers must be between 1 and 8");
    }
    if (spec.startingStack == 0) throw std::invalid_argument("startingStack must be positive");

    CompetitionSummary summary{
        .name = nextCompetitionName(),
        .style = CompetitionStyle::tournament,
        .tournament = spec,
        .tables = {{.name = "Red", .maximumSeats = spec.maximumPlayers}},
    };
    competitions_.push_back({.summary = std::move(summary)});
    return summaryOf(competitions_.back());
}

House::Competition& House::findCompetition(std::string_view name) {
    const auto requested = normalizeName(name);
    const auto found = std::ranges::find_if(competitions_, [&requested](const Competition& competition) {
        return normalizeName(competition.summary.name) == requested;
    });
    if (found == competitions_.end()) throw std::invalid_argument("competition was not found");
    return *found;
}

const House::Competition* House::findCompetition(std::string_view name) const {
    const auto requested = normalizeName(name);
    const auto found = std::ranges::find_if(competitions_, [&requested](const Competition& competition) {
        return normalizeName(competition.summary.name) == requested;
    });
    return found == competitions_.end() ? nullptr : &*found;
}

std::string House::nextReferencePlayerName(const Competition& competition) {
    constexpr std::array animals{"Fox", "Badger", "Otter", "Lynx", "Raven", "Puma", "Marten", "Heron", "Wolf", "Falcon"};
    std::uniform_int_distribution<std::size_t> animal(0, animals.size() - 1);
    std::uniform_int_distribution<int> suffix(10, 99);
    for (int attempts = 0; attempts < 500; ++attempts) {
        const auto candidate = std::string{animals[animal(random_)]} + std::to_string(suffix(random_));
        const auto exists = std::ranges::any_of(competition.players, [&candidate](const auto& player) {
            return player.player->name() == candidate;
        });
        if (!exists) return candidate;
    }
    throw std::runtime_error("could not assign a unique reference-player name");
}

CompetitionSummary House::createReferencePlayers(std::string_view competitionName, std::size_t count) {
    auto& competition = findCompetition(competitionName);
    const auto occupiedSeats = competition.players.size();
    std::size_t capacity = 0;
    for (const auto& table : competition.summary.tables) capacity += table.maximumSeats;
    const auto availableSeats = capacity - occupiedSeats;
    if (count == 0 || count > 7 || count > availableSeats) {
        throw std::invalid_argument("reference-player count does not fit the available seats");
    }

    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    for (std::size_t index = 0; index < count; ++index) {
        std::size_t tableIndex = 0;
        std::size_t seat = 0;
        for (; tableIndex < competition.summary.tables.size() && seat == 0; ++tableIndex) {
            const auto& table = competition.summary.tables[tableIndex];
            for (std::size_t candidateSeat = 1; candidateSeat <= table.maximumSeats; ++candidateSeat) {
                const auto occupied = std::ranges::any_of(competition.players, [tableIndex, candidateSeat](const auto& player) {
                    return player.tableIndex == tableIndex && player.seat == candidateSeat;
                });
                if (!occupied) {
                    seat = candidateSeat;
                    break;
                }
            }
        }
        if (seat == 0) throw std::runtime_error("could not find a free seat");
        competition.players.push_back({
            .player = std::make_unique<ReferencePlayer>(nextReferencePlayerName(competition), ReferencePlayerProfile{
                .riskTolerance = disposition(random_),
                .optimism = disposition(random_),
                .variability = disposition(random_),
            }),
            .tableIndex = tableIndex - 1,
            .seat = seat,
        });
    }
    return summaryOf(competition);
}

CompetitionSummary House::summaryOf(const Competition& competition) const {
    auto summary = competition.summary;
    for (auto& table : summary.tables) table.players.clear();
    for (const auto& player : competition.players) {
        summary.tables[player.tableIndex].players.push_back({
            .player = {.name = player.player->name(), .kind = player.player->kind()},
            .seat = player.seat,
        });
    }
    return summary;
}

std::vector<CompetitionSummary> House::competitions() const {
    std::vector<CompetitionSummary> summaries;
    summaries.reserve(competitions_.size());
    for (const auto& competition : competitions_) summaries.push_back(summaryOf(competition));
    return summaries;
}

std::optional<CompetitionSummary> House::competition(std::string_view name) const {
    const auto* found = findCompetition(name);
    if (found == nullptr) return std::nullopt;
    return summaryOf(*found);
}

} // namespace bluffskill::poker
