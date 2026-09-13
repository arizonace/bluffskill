#include "bluffskill/app_config/app_config.hpp"

#include <cassert>

int main() {
    const bluffskill::app_config::Settings settings;

    assert(settings.chipDenominations.size() == 4);
    assert(settings.chipDenominations.at(0) == 25);
    assert(settings.chipDenominations.at(1) == 100);
    assert(settings.chipDenominations.at(2) == 500);
    assert(settings.chipDenominations.at(3) == 1000);

    assert(settings.serverPreferredPorts.size() == 3);
    assert(settings.serverPreferredPorts.at(0) == 53153);
    assert(settings.serverPreferredPorts.at(1) == 53154);
    assert(settings.serverPreferredPorts.at(2) == 53155);

    return 0;
}
