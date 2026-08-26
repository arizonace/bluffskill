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
        .tables = {{.name = "Red", .seats = 0}},
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

std::string House::nextReferencePlayerName(const Competition& competition) {
    constexpr std::array animals{"Fox", "Badger", "Otter", "Lynx", "Raven", "Puma", "Marten", "Heron", "Wolf", "Falcon"};
    std::uniform_int_distribution<std::size_t> animal(0, animals.size() - 1);
    std::uniform_int_distribution<int> suffix(10, 99);
    for (int attempts = 0; attempts < 500; ++attempts) {
        const auto candidate = std::string{animals[animal(random_)]} + std::to_string(suffix(random_));
        const auto exists = std::ranges::any_of(competition.players, [&candidate](const auto& player) {
            return player->name() == candidate;
        });
        if (!exists) return candidate;
    }
    throw std::runtime_error("could not assign a unique reference-player name");
}

CompetitionSummary House::createReferencePlayers(std::string_view competitionName, std::size_t count) {
    auto& competition = findCompetition(competitionName);
    const auto occupiedSeats = competition.players.size();
    if (count == 0 || count > 7 || count > competition.summary.tournament.maximumPlayers - occupiedSeats) {
        throw std::invalid_argument("reference-player count does not fit the available seats");
    }

    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    for (std::size_t index = 0; index < count; ++index) {
        competition.players.push_back(std::make_unique<ReferencePlayer>(nextReferencePlayerName(competition), ReferencePlayerProfile{
            .riskTolerance = disposition(random_),
            .optimism = disposition(random_),
            .variability = disposition(random_),
        }));
    }
    competition.summary.tables.front().seats = competition.players.size();
    return summaryOf(competition);
}

CompetitionSummary House::summaryOf(const Competition& competition) const {
    auto summary = competition.summary;
    summary.players.clear();
    for (const auto& player : competition.players) {
        summary.players.push_back({.name = player->name(), .kind = player->kind()});
    }
    return summary;
}

std::vector<CompetitionSummary> House::competitions() const {
    std::vector<CompetitionSummary> summaries;
    summaries.reserve(competitions_.size());
    for (const auto& competition : competitions_) summaries.push_back(summaryOf(competition));
    return summaries;
}

} // namespace bluffskill::poker
