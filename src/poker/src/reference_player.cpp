#include "bluffskill/poker/reference_player.hpp"

namespace bluffskill::poker {

std::string_view toString(PlayerKind kind) noexcept {
    switch (kind) {
    case PlayerKind::api: return "API";
    case PlayerKind::reference: return "Reference";
    }
    return "Unknown";
}

std::string_view toString(ReferencePlayerType type) noexcept {
    switch (type) {
    case ReferencePlayerType::leo: return "Leo";
    case ReferencePlayerType::virgo: return "Virgo";
    }
    return "Unknown";
}

} // namespace bluffskill::poker
