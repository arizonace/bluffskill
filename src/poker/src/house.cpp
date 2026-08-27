#include "bluffskill/poker/house.hpp"
#include "bluffskill/poker/api_player.hpp"
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
    Competition competition{.summary = std::move(summary)};
    competition.tables.push_back(std::make_unique<Table>(competition.summary.tables.front().name,
        competition.summary.tables.front().maximumSeats, static_cast<Chips>(spec.startingStack)));
    competitions_.push_back(std::move(competition));
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

std::size_t House::findTableIndex(const Competition& competition, std::string_view tableName) const {
    const auto requested = normalizeName(tableName);
    const auto found = std::ranges::find_if(competition.summary.tables, [&requested](const TableSummary& table) {
        return normalizeName(table.name) == requested;
    });
    if (found == competition.summary.tables.end()) throw std::invalid_argument("table was not found");
    return static_cast<std::size_t>(std::distance(competition.summary.tables.begin(), found));
}

Table& House::findTable(Competition& competition, std::string_view tableName) {
    return *competition.tables.at(findTableIndex(competition, tableName));
}

const Table& House::findTable(const Competition& competition, std::string_view tableName) const {
    return *competition.tables.at(findTableIndex(competition, tableName));
}

std::optional<std::size_t> House::firstFreeSeat(const Competition& competition, std::size_t tableIndex) const {
    const auto& table = competition.summary.tables.at(tableIndex);
    for (std::size_t candidateSeat = 1; candidateSeat <= table.maximumSeats; ++candidateSeat) {
        const auto occupied = std::ranges::any_of(competition.players, [tableIndex, candidateSeat](const auto& player) {
            return player.tableIndex == tableIndex && player.seat == candidateSeat;
        });
        if (!occupied) return candidateSeat;
    }
    return std::nullopt;
}

void House::validatePlayerName(const Competition& competition, std::string_view name) const {
    if (name.empty() || !std::ranges::all_of(name, [](unsigned char character) {
        return std::isalnum(character) || character == '-';
    })) {
        throw std::invalid_argument("player name must contain only letters, digits, and dashes");
    }
    const auto normalized = normalizeName(name);
    const auto exists = std::ranges::any_of(competition.players, [&normalized](const auto& player) {
        return normalizeName(player.player->name()) == normalized;
    });
    if (exists) throw std::invalid_argument("player name is already in use for this competition");
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
        std::optional<std::size_t> seat;
        for (; tableIndex < competition.summary.tables.size() && !seat; ++tableIndex) {
            seat = firstFreeSeat(competition, tableIndex);
        }
        if (!seat) throw std::runtime_error("could not find a free seat");
        const auto playerName = nextReferencePlayerName(competition);
        competition.players.push_back({
            .player = std::make_unique<ReferencePlayer>(playerName, ReferencePlayerProfile{
                .riskTolerance = disposition(random_),
                .optimism = disposition(random_),
                .variability = disposition(random_),
            }),
            .tableIndex = tableIndex - 1,
            .seat = *seat,
        });
        competition.tables.at(tableIndex - 1)->seatPlayer(playerName, PlayerKind::reference, *seat,
            static_cast<Chips>(competition.summary.tournament.startingStack));
    }
    return summaryOf(competition);
}

CompetitionSummary House::createApiPlayer(std::string_view competitionName, std::string_view tableName, std::string name) {
    auto& competition = findCompetition(competitionName);
    validatePlayerName(competition, name);
    const auto tableIndex = findTableIndex(competition, tableName);
    const auto seat = firstFreeSeat(competition, tableIndex);
    if (!seat) throw std::invalid_argument("table has no free seats");
    competition.players.push_back({
        .player = std::make_unique<ApiPlayer>(std::move(name)),
        .tableIndex = tableIndex,
        .seat = *seat,
    });
    const auto& player = competition.players.back();
    auto& table = *competition.tables.at(tableIndex);
    table.seatPlayer(player.player->name(), player.player->kind(), *seat, static_cast<Chips>(competition.summary.tournament.startingStack));
    if (table.viewFor().street == Street::waiting) {
        table.startHand();
        advanceReferencePlayers(competition, tableIndex);
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

TableView House::tableView(std::string_view competitionName, std::string_view tableName, std::string_view viewerName) const {
    const auto* competition = findCompetition(competitionName);
    if (competition == nullptr) throw std::invalid_argument("competition was not found");
    return findTable(*competition, tableName).viewFor(viewerName);
}

void House::submitAction(std::string_view competitionName, std::string_view tableName, std::string_view playerName,
    Action action, Chips amount, std::uint64_t expectedSequence) {
    auto& competition = findCompetition(competitionName);
    const auto tableIndex = findTableIndex(competition, tableName);
    competition.tables.at(tableIndex)->submitAction(playerName, action, amount, expectedSequence);
    advanceReferencePlayers(competition, tableIndex);
}

void House::startNextHand(std::string_view competitionName, std::string_view tableName) {
    auto& competition = findCompetition(competitionName);
    const auto tableIndex = findTableIndex(competition, tableName);
    competition.tables.at(tableIndex)->startNextHand();
    advanceReferencePlayers(competition, tableIndex);
}

void House::restartTable(std::string_view competitionName, std::string_view tableName) {
    auto& competition = findCompetition(competitionName);
    const auto tableIndex = findTableIndex(competition, tableName);
    competition.tables.at(tableIndex)->restartGame();
    advanceReferencePlayers(competition, tableIndex);
}

void House::advanceReferencePlayers(Competition& competition, std::size_t tableIndex) {
    auto& table = *competition.tables.at(tableIndex);
    // A valid no-limit hand has a finite stack of actions. The guard makes a broken
    // policy fail loudly rather than consuming the server event loop indefinitely.
    for (std::size_t steps = 0; steps < 10'000; ++steps) {
        const auto publicView = table.viewFor();
        if (!publicView.actingSeat) return;
        const auto actor = std::ranges::find_if(competition.players, [tableIndex, seat = *publicView.actingSeat](const auto& player) {
            return player.tableIndex == tableIndex && player.seat == seat;
        });
        if (actor == competition.players.end()) throw std::logic_error("the acting seat has no player identity");
        if (actor->player->kind() != PlayerKind::reference) return;

        const auto& referencePlayer = static_cast<const ReferencePlayer&>(*actor->player);
        const auto privateView = table.viewFor(referencePlayer.name());
        const auto decision = referencePlayer.chooseResponse(privateView, random_);
        table.submitAction(referencePlayer.name(), decision.action, decision.amount, privateView.eventSequence);
    }
    throw std::logic_error("reference-player policy exceeded the automated-action limit");
}

} // namespace bluffskill::poker
