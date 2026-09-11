#pragma once

#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

// The September 2026 Virgo policy retained as a stable reference opponent.
// Values are normalized to [0, 1].
struct AugustVirgoReferencePlayerProfile {
    double curiosity;
    double hope;
    double empathy;
    double longevity;
};

class AugustVirgoReferencePlayer final : public ReferencePlayerController {
public:
    AugustVirgoReferencePlayer(std::string name, AugustVirgoReferencePlayerProfile profile);

    [[nodiscard]] ReferencePlayerType referenceType() const noexcept override { return ReferencePlayerType::augustVirgo; }
    [[nodiscard]] const AugustVirgoReferencePlayerProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] std::vector<ReferencePlayerParameter> parameters() const override;
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const override;

private:
    AugustVirgoReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
