#pragma once

#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

// Libra is a reproducible, value-first baseline. It has no personality
// profile, does not bluff, and uses the documented Libra Canon v1 rules.
class LibraReferencePlayer final : public ReferencePlayerController {
public:
    explicit LibraReferencePlayer(std::string name);

    [[nodiscard]] ReferencePlayerType referenceType() const noexcept override { return ReferencePlayerType::libra; }
    [[nodiscard]] std::vector<ReferencePlayerParameter> parameters() const override;
    [[nodiscard]] ReferenceDecision chooseResponse(const TableView& privateView, std::mt19937_64& random) const override;
};

} // namespace bluffskill::poker
