#include "bluffskill/poker/api_player.hpp"

namespace bluffskill::poker {

ApiPlayer::ApiPlayer(std::string name, bool recordHoleCardsWhenFolding)
    : Player(std::move(name)), recordHoleCardsWhenFolding_(recordHoleCardsWhenFolding) {}

} // namespace bluffskill::poker
