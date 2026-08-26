#pragma once

#include "bluffskill/poker/player.hpp"

namespace bluffskill::poker {

// These values will later guide the reference policy without changing the Player interface.
struct ReferencePlayerProfile {
    double riskTolerance;
    double optimism;
    double variability;
};

class ReferencePlayer final : public Player {
public:
    ReferencePlayer(std::string name, ReferencePlayerProfile profile);

    [[nodiscard]] PlayerKind kind() const noexcept override { return PlayerKind::reference; }
    [[nodiscard]] const ReferencePlayerProfile& profile() const noexcept { return profile_; }

private:
    ReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
