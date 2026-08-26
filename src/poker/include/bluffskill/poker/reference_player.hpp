#pragma once

#include "bluffskill/poker/player.hpp"
#include "bluffskill/poker/table.hpp"

#include <random>

namespace bluffskill::poker {

// Each value is normalized to [0, 1]. They guide a local reference-controller policy;
// they never cross an API boundary or change the server-facing player identity.
struct ReferencePlayerProfile {
    double riskTolerance;
    double optimism;
    double variability;
};

struct ReferenceDecision {
    Action action{Action::fold};
    Chips amount{0}; // Total commitment in the current betting round for bet/raise.
};

class ReferencePlayer final : public Player {
public:
    ReferencePlayer(std::string name, ReferencePlayerProfile profile);

    [[nodiscard]] PlayerKind kind() const noexcept override { return PlayerKind::reference; }
    [[nodiscard]] const ReferencePlayerProfile& profile() const noexcept { return profile_; }
    // The policy consumes the same private TableView available to a player controller.
    // The Table remains authoritative and validates the returned command.
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const;

private:
    ReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
