#pragma once

#include "bluffskill/poker/player.hpp"
#include "bluffskill/poker/table.hpp"

#include <random>
#include <vector>

namespace bluffskill::poker {

struct ReferenceDecision {
    Action action{Action::fold};
    Chips amount{0}; // Total commitment in the current betting round for bet/raise.
};

// A parameter is available to the server console for local diagnostics. It is
// not included in TableView or REST projections because bot profiles are private.
struct ReferencePlayerParameter {
    std::string name;
    double value{0.0};
};

struct ReferencePlayerInspection {
    ReferencePlayerType type{ReferencePlayerType::leo};
    std::vector<ReferencePlayerParameter> parameters;
};

class ReferencePlayerController : public Player {
public:
    using Player::Player;
    ~ReferencePlayerController() override = default;

    [[nodiscard]] PlayerKind kind() const noexcept final { return PlayerKind::reference; }
    [[nodiscard]] virtual ReferencePlayerType referenceType() const noexcept = 0;
    [[nodiscard]] virtual std::vector<ReferencePlayerParameter> parameters() const = 0;
    // The policy consumes the same private TableView available to a player controller.
    // The Table remains authoritative and validates the returned command.
    [[nodiscard]] virtual ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const = 0;
};

} // namespace bluffskill::poker
