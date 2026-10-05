#include "bluffskill/app_config/app_config.hpp"

#include <cassert>

#include <QFile>

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
    assert(settings.smallBlind == 25);
    assert(settings.stack == 7'500);
    assert(!settings.detailedServerLogs);
    assert(!settings.storeActionsInJson);

    assert(settings.serverActionLogVisibleColumns.contains("Timestamp"));
    assert(settings.serverActionLogVisibleColumns.contains("Game"));
    assert(settings.serverActionLogVisibleColumns.contains("Hand"));
    assert(!settings.serverActionLogVisibleColumns.contains("Round"));
    assert(settings.serverActionLogVisibleColumns.contains("Details"));
    assert(!settings.serverActionLogVisibleColumns.contains("Index"));
    assert(!settings.serverActionLogVisibleColumns.contains("Hole"));

    const auto configPath = bluffskill::app_config::AppConfig::filePath();
    assert(configPath.startsWith(qEnvironmentVariable("HOME")));
    QFile::remove(configPath);

    auto serverSettings = settings;
    serverSettings.smallBlind = 100;
    serverSettings.blindHandsPerLevel = 2;
    serverSettings.blindMinutesPerLevel = 3;
    bluffskill::app_config::AppConfig::save(serverSettings);

    auto staleClientSettings = settings;
    staleClientSettings.defaultPlayerName = "Client Player";
    staleClientSettings.soundEffects = false;
    staleClientSettings.blindHandsPerLevel = 10'000;
    staleClientSettings.blindMinutesPerLevel = 3'600;
    bluffskill::app_config::AppConfig::saveClientSettings(staleClientSettings);

    const auto persisted = bluffskill::app_config::AppConfig::load();
    assert(persisted.smallBlind == 100);
    assert(persisted.blindHandsPerLevel == 2);
    assert(persisted.blindMinutesPerLevel == 3);
    assert(persisted.defaultPlayerName == "Client Player");
    assert(!persisted.soundEffects);

    return 0;
}
