#include "bluffskill/poker/house.hpp"
#include "bluffskill/poker/api_player.hpp"
#include "bluffskill/poker/august_leo_reference_player.hpp"
#include "bluffskill/poker/august_virgo_reference_player.hpp"
#include "bluffskill/poker/leo_reference_player.hpp"
#include "bluffskill/poker/reference_player.hpp"
#include "bluffskill/poker/virgo_reference_player.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace bluffskill::poker {

namespace {

constexpr std::array competitionNames{
    "Aldebaran", "Alexandria", "Andromeda", "Arcadia", "Asgard", "Athens", "Avalon", "Babylon",
    "Belfast", "Borealis", "Byzantium", "Cairo", "Caledonia", "Camelot", "Carthage", "Cascadia",
    "Chimera", "Cimmeria", "Columbia", "Corinth", "Cyrene", "Delphi", "Eldoria", "Elysium",
    "Everest", "Florence", "Gaia", "Geneva", "Gotham", "Granada", "Havana", "Helios",
    "Hyperion", "Icarus", "Ionia", "Istanbul", "Juno", "Kilimanjaro", "Kingston", "Kyoto",
    "Laguna", "Lisbon", "Lumeria", "Luxor", "Madison", "Meridian", "Monaco", "Montreal",
    "Nairobi", "Nebula", "Newcastle", "Nimrod", "Olympia", "Orion", "Oslo", "Palmyra",
    "Persepolis", "Phoenix", "Prague", "Rivendell", "Sahara", "Samarkand", "Triton", "Zephyr",
};

constexpr std::array referencePlayerNames{
    "Aardvark", "Albatross", "Alpaca", "Antelope", "Armadillo", "Badger", "Barracuda", "Beaver",
    "Bison", "Bobcat", "Buffalo", "Butterfly", "Camel", "Caribou", "Cheetah", "Condor",
    "Cougar", "Coyote", "Crane", "Dolphin", "Eagle", "Falcon", "Ferret", "Firefly",
    "Flamingo", "Fox", "Gazelle", "Gecko", "Giraffe", "Goshawk", "Heron", "Hummingbird",
    "Hyena", "Ibis", "Jaguar", "Jay", "Kestrel", "Koala", "Lemur", "Leopard",
    "Lynx", "Macaque", "Magpie", "Manatee", "Marten", "Meerkat", "Mongoose", "Moose",
    "Narwhal", "Newt", "Nightingale", "Ocelot", "Octopus", "Orca", "Osprey", "Otter",
    "Owl", "Panda", "Panther", "Parrot", "Peacock", "Pelican", "Peregrine", "Puma",
    "Quail", "Raccoon", "Raven", "RedPanda", "Reindeer", "Rhino", "Salamander", "Seal",
    "Serval", "Shark", "Skylark", "Sloth", "Sparrow", "Stoat", "Swan", "Tapir",
    "Tiger", "Toucan", "Turtle", "Viper", "Walrus", "Weasel", "Whale", "Wildcat",
    "Wolf", "Wolverine", "Wombat", "Woodpecker", "Yak", "Zebra", "Auk", "Beluga",
    "Capybara", "Cassowary", "Chameleon", "Chipmunk", "Cormorant", "Dingo", "Dragonfly", "Egret",
    "Fennec", "Goldfinch", "Grouse", "Kingfisher", "Lobster", "Manta", "Nuthatch", "Puffin",
    "Sable", "Starling", "Tamarin", "Tern", "Vicuna", "Wagtail", "Zorilla", "Kangaroo",
};

template <typename T, std::size_t size>
std::vector<std::string> shuffledNamePool(const std::array<T, size>& names, std::mt19937_64& random) {
    std::vector<std::string> pool;
    pool.reserve(names.size());
    for (const auto name : names) pool.emplace_back(name);
    std::ranges::shuffle(pool, random);
    return pool;
}

std::string normalizeName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return normalized;
}

using ReferencePlayerFactory = std::unique_ptr<ReferencePlayerController> (*)(std::string, std::mt19937_64&);

struct ReferencePlayerRegistration {
    ReferencePlayerType type;
    ReferencePlayerFactory create;
};

std::unique_ptr<ReferencePlayerController> createLeo(std::string name, std::mt19937_64& random) {
    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    return std::make_unique<LeoReferencePlayer>(std::move(name), LeoReferencePlayerProfile{
        .riskTolerance = disposition(random), .optimism = disposition(random), .variability = disposition(random),
    });
}

std::unique_ptr<ReferencePlayerController> createAugustLeo(std::string name, std::mt19937_64& random) {
    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    return std::make_unique<AugustLeoReferencePlayer>(std::move(name), AugustLeoReferencePlayerProfile{
        .riskTolerance = disposition(random), .optimism = disposition(random), .variability = disposition(random),
    });
}

std::unique_ptr<ReferencePlayerController> createVirgo(std::string name, std::mt19937_64& random) {
    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    return std::make_unique<VirgoReferencePlayer>(std::move(name), VirgoReferencePlayerProfile{
        .curiosity = disposition(random), .hope = disposition(random), .empathy = disposition(random), .longevity = disposition(random),
    });
}

std::unique_ptr<ReferencePlayerController> createAugustVirgo(std::string name, std::mt19937_64& random) {
    std::uniform_real_distribution<double> disposition(0.15, 0.85);
    return std::make_unique<AugustVirgoReferencePlayer>(std::move(name), AugustVirgoReferencePlayerProfile{
        .curiosity = disposition(random), .hope = disposition(random), .empathy = disposition(random), .longevity = disposition(random),
    });
}

constexpr std::array referencePlayerRegistrations{
    ReferencePlayerRegistration{ReferencePlayerType::leo, createLeo},
    ReferencePlayerRegistration{ReferencePlayerType::augustLeo, createAugustLeo},
    ReferencePlayerRegistration{ReferencePlayerType::virgo, createVirgo},
    ReferencePlayerRegistration{ReferencePlayerType::augustVirgo, createAugustVirgo},
};

} // namespace

House::House(BlindSchedule blindSchedule, ChipDenominations chipDenominations)
    : blindSchedule_(blindSchedule), chipDenominations_(normalizedChipDenominations(std::move(chipDenominations))),
      competitionNamePool_(shuffledNamePool(competitionNames, random_)),
      referencePlayerNamePool_(shuffledNamePool(referencePlayerNames, random_)) {}

std::string House::nextCompetitionName() {
    if (nextCompetitionNameIndex_ == competitionNamePool_.size()) throw std::runtime_error("competition name pool is exhausted");
    return competitionNamePool_.at(nextCompetitionNameIndex_++);
}

CompetitionSummary House::createSingleTableTournament(TournamentSpec spec) {
    if (spec.maximumPlayers == 0 || spec.maximumPlayers > 10) {
        throw std::invalid_argument("maximumPlayers must be between 1 and 10");
    }
    if (spec.startingStack == 0) throw std::invalid_argument("startingStack must be positive");

    CompetitionSummary summary{
        .name = nextCompetitionName(),
        .style = CompetitionStyle::tournament,
        .tournament = spec,
        .chipDenominations = chipDenominations_,
        .tables = {{.name = "Red", .maximumSeats = spec.maximumPlayers}},
    };
    Competition competition{.summary = std::move(summary)};
    competition.tables.push_back(std::make_unique<Table>(competition.summary.tables.front().name,
        competition.summary.tables.front().maximumSeats, static_cast<Chips>(spec.startingStack), blindSchedule_, chipDenominations_));
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
    while (nextReferencePlayerNameIndex_ < referencePlayerNamePool_.size()) {
        const auto candidate = referencePlayerNamePool_.at(nextReferencePlayerNameIndex_++);
        const auto exists = std::ranges::any_of(competition.players, [&candidate](const auto& player) {
            return normalizeName(player.player->name()) == normalizeName(candidate);
        });
        if (!exists) return candidate;
    }
    throw std::runtime_error("reference-player name pool is exhausted");
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

CompetitionSummary House::createReferencePlayers(std::string_view competitionName, std::size_t count, ReferencePlayerType type) {
    auto& competition = findCompetition(competitionName);
    const auto occupiedSeats = competition.players.size();
    std::size_t capacity = 0;
    for (const auto& table : competition.summary.tables) capacity += table.maximumSeats;
    const auto availableSeats = capacity - occupiedSeats;
    if (count == 0 || count > availableSeats) {
        throw std::invalid_argument("reference-player count does not fit the available seats");
    }

    const auto registration = std::ranges::find(referencePlayerRegistrations, type, &ReferencePlayerRegistration::type);
    if (registration == referencePlayerRegistrations.end()) throw std::invalid_argument("reference-player type is not registered");
    for (std::size_t index = 0; index < count; ++index) {
        std::size_t tableIndex = 0;
        std::optional<std::size_t> seat;
        for (; tableIndex < competition.summary.tables.size() && !seat; ++tableIndex) {
            seat = firstFreeSeat(competition, tableIndex);
        }
        if (!seat) throw std::runtime_error("could not find a free seat");
        const auto playerName = nextReferencePlayerName(competition);
        auto referencePlayer = registration->create(playerName, random_);
        competition.players.push_back({
            .player = std::move(referencePlayer),
            .tableIndex = tableIndex - 1,
            .seat = *seat,
        });
        competition.tables.at(tableIndex - 1)->seatPlayer(playerName, PlayerKind::reference, *seat,
            static_cast<Chips>(competition.summary.tournament.startingStack));
        startTableIfReady(competition, tableIndex - 1);
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
    startTableIfReady(competition, tableIndex);
    return summaryOf(competition);
}

void House::startTableIfReady(Competition& competition, std::size_t tableIndex) {
    auto& table = *competition.tables.at(tableIndex);
    if (table.viewFor().street != Street::waiting) return;
    const auto seated = std::count_if(competition.players.begin(), competition.players.end(), [tableIndex](const auto& player) {
        return player.tableIndex == tableIndex;
    });
    if (seated != competition.summary.tables.at(tableIndex).maximumSeats) return;
    table.startHand();
    advanceReferencePlayers(competition, tableIndex);
}

CompetitionSummary House::summaryOf(const Competition& competition) const {
    auto summary = competition.summary;
    for (auto& table : summary.tables) table.players.clear();
    for (const auto& player : competition.players) {
        std::optional<ReferencePlayerType> referenceType;
        if (player.player->kind() == PlayerKind::reference) {
            referenceType = static_cast<const ReferencePlayerController&>(*player.player).referenceType();
        }
        summary.tables[player.tableIndex].players.push_back({
            .player = {.name = player.player->name(), .kind = player.player->kind(), .referenceType = referenceType},
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

std::vector<ReferencePlayerType> House::referencePlayerTypes() const {
    std::vector<ReferencePlayerType> types;
    types.reserve(referencePlayerRegistrations.size());
    for (const auto& registration : referencePlayerRegistrations) types.push_back(registration.type);
    return types;
}

TableView House::tableView(std::string_view competitionName, std::string_view tableName, std::string_view viewerName) const {
    const auto* competition = findCompetition(competitionName);
    if (competition == nullptr) throw std::invalid_argument("competition was not found");
    return findTable(*competition, tableName).viewFor(viewerName);
}

std::optional<ReferencePlayerInspection> House::referencePlayerInspection(
    std::string_view competitionName, std::string_view tableName, std::string_view playerName) const {
    const auto* competition = findCompetition(competitionName);
    if (competition == nullptr) return std::nullopt;
    const auto tableIndex = findTableIndex(*competition, tableName);
    const auto player = std::ranges::find_if(competition->players, [tableIndex, playerName](const auto& candidate) {
        return candidate.tableIndex == tableIndex && candidate.player->name() == playerName;
    });
    if (player == competition->players.end() || player->player->kind() != PlayerKind::reference) return std::nullopt;
    const auto& referencePlayer = static_cast<const ReferencePlayerController&>(*player->player);
    return ReferencePlayerInspection{.type = referencePlayer.referenceType(), .parameters = referencePlayer.parameters()};
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

void House::setBlindSchedule(BlindSchedule blindSchedule) {
    for (auto& competition : competitions_) {
        for (auto& table : competition.tables) table->setBlindSchedule(blindSchedule);
    }
    blindSchedule_ = blindSchedule;
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

        const auto& referencePlayer = static_cast<const ReferencePlayerController&>(*actor->player);
        const auto privateView = table.viewFor(referencePlayer.name());
        const auto decision = referencePlayer.chooseResponse(privateView, random_);
        table.submitAction(referencePlayer.name(), decision.action, decision.amount, privateView.eventSequence);
    }
    throw std::logic_error("reference-player policy exceeded the automated-action limit");
}

} // namespace bluffskill::poker
