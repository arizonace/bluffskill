#pragma once

#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

// Each value is normalized to [0, 1]. Leo's policy is assertive: risk and
// optimism favor action, while variability makes individual decisions less predictable.
struct LeoReferencePlayerProfile {
    double riskTolerance;
    double optimism;
    double variability;
};

class LeoReferencePlayer final : public ReferencePlayerController {
public:
    LeoReferencePlayer(std::string name, LeoReferencePlayerProfile profile);

    [[nodiscard]] ReferencePlayerType referenceType() const noexcept override { return ReferencePlayerType::leo; }
    [[nodiscard]] const LeoReferencePlayerProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] std::vector<ReferencePlayerParameter> parameters() const override;
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const override;

private:
    LeoReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
