#pragma once

#include "bluffskill/poker/player.hpp"

namespace bluffskill::poker {

// The server-facing identity for every remote player, regardless of how its client chooses actions.
class ApiPlayer final : public Player {
public:
    // This is a local export-consent preference, not a player kind or
    // controller attribute.  It is never exposed in public table views.
    explicit ApiPlayer(std::string name, bool recordHoleCardsWhenFolding = false);

    [[nodiscard]] PlayerKind kind() const noexcept override { return PlayerKind::api; }
    [[nodiscard]] bool recordsHoleCardsWhenFolding() const noexcept { return recordHoleCardsWhenFolding_; }

private:
    bool recordHoleCardsWhenFolding_{false};
};

} // namespace bluffskill::poker
