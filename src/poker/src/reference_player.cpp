#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

ReferencePlayer::ReferencePlayer(std::string name, ReferencePlayerProfile profile)
    : Player(std::move(name)), profile_(profile) {}

std::string_view toString(PlayerKind kind) noexcept {
    switch (kind) {
    case PlayerKind::api: return "API";
    case PlayerKind::reference: return "Reference";
    }
    return "Unknown";
}

} // namespace bluffskill::poker
