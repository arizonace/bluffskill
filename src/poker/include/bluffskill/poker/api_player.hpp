#pragma once

#include "bluffskill/poker/player.hpp"

namespace bluffskill::poker {

// The server-facing identity for every remote player, regardless of how its client chooses actions.
class ApiPlayer final : public Player {
public:
    explicit ApiPlayer(std::string name);

    [[nodiscard]] PlayerKind kind() const noexcept override { return PlayerKind::api; }
};

} // namespace bluffskill::poker
