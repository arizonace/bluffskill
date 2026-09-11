#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace bluffskill::poker {

enum class PlayerKind { api, reference };
enum class ReferencePlayerType { leo, augustLeo, virgo, augustVirgo };

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
    std::optional<ReferencePlayerType> referenceType;
};

[[nodiscard]] std::string_view toString(PlayerKind kind) noexcept;
[[nodiscard]] std::string_view toString(ReferencePlayerType type) noexcept;

} // namespace bluffskill::poker
