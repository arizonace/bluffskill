#pragma once

#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

// Each value is normalized to [0, 1]. Virgo's policy weighs visible board
// danger and opponents' public actions against its own made hands and draws.
struct VirgoReferencePlayerProfile {
    double curiosity;
    double hope;
    double empathy;
    double longevity;
};

class VirgoReferencePlayer final : public ReferencePlayerController {
public:
    VirgoReferencePlayer(std::string name, VirgoReferencePlayerProfile profile);

    [[nodiscard]] ReferencePlayerType referenceType() const noexcept override { return ReferencePlayerType::virgo; }
    [[nodiscard]] const VirgoReferencePlayerProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] std::vector<ReferencePlayerParameter> parameters() const override;
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const override;

private:
    VirgoReferencePlayerProfile profile_;
};

} // namespace bluffskill::poker
