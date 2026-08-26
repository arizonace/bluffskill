#pragma once

#include <string>
#include <string_view>

namespace bluffskill::poker {

enum class PlayerKind { api, reference };

// A player is an identity. How it chooses an action belongs to a controller implementation.
class Player {
public:
    virtual ~Player() = default;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] virtual PlayerKind kind() const noexcept = 0;

protected:
    explicit Player(std::string name) : name_(std::move(name)) {}

private:
    std::string name_;
};

struct PlayerSummary {
    std::string name;
    PlayerKind kind;
};

[[nodiscard]] std::string_view toString(PlayerKind kind) noexcept;

} // namespace bluffskill::poker
