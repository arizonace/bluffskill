#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace bluffskill::app_config {

struct Settings {
    Settings() {
        chipDenominations.append(25);
        chipDenominations.append(100);
        chipDenominations.append(500);
        chipDenominations.append(1000);
        serverPreferredPorts.append(53153);
        serverPreferredPorts.append(53154);
        serverPreferredPorts.append(53155);
    }

    int playerClockSeconds{120};
    int dealClockSeconds{20};
    int uninterruptedDealerDelayMilliseconds{5'000};
    int automatedPlayerDelayMilliseconds{1'000};
    qint64 smallBlind{25};
    qint64 stack{7'500};
    int blindHandsPerLevel{16};
    int blindMinutesPerLevel{20};
    QList<qint64> chipDenominations;
    QString defaultPlayerName{"Player"};
    QList<quint16> serverPreferredPorts;
    bool clientAutoConnect{false};
    bool soundEffects{true};
    bool detailedServerLogs{false};
    // Only optional server action-log columns appear here.  The server keeps
    // player, action, and value visible; hole cards are never shown.
    QStringList serverActionLogVisibleColumns{"Timestamp", "Game", "Round", "Street", "Kind", "Stack", "Gain", "Pot", "Hand"};
};

class AppConfig final {
public:
    [[nodiscard]] static QString filePath();
    [[nodiscard]] static Settings load();
    static void save(const Settings& settings);
};

} // namespace bluffskill::app_config
