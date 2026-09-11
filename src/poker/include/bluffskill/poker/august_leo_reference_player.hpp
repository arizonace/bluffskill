#pragma once

#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

// The September 2026 Leo policy retained as a stable reference opponent.
// Values are normalized to [0, 1].
struct AugustLeoReferencePlayerProfile {
    double riskTolerance;
    double optimism;
    double variability;
};

class AugustLeoReferencePlayer final : public ReferencePlayerController {
public:
    AugustLeoReferencePlayer(std::string name, AugustLeoReferencePlayerProfile profile);

    [[nodiscard]] ReferencePlayerType referenceType() const noexcept override { return ReferencePlayerType::leo; }
    [[nodiscard]] const AugustLeoReferencePlayerProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] std::vector<ReferencePlayerParameter> parameters() const override;
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const override;

private:
    AugustLeoReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
