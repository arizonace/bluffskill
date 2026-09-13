#pragma once

#include <QList>
#include <QString>

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
    int smallBlind{1}; // Number of smallest chip.
    int blindHandsPerLevel{16};
    int blindMinutesPerLevel{20};
    QList<qint64> chipDenominations;
    QString defaultPlayerName{"Player"};
    QList<quint16> serverPreferredPorts;
    bool clientAutoConnect{false};
    bool soundEffects{true};
    bool detailedServerLogs{false};
};

class AppConfig final {
public:
    [[nodiscard]] static QString filePath();
    [[nodiscard]] static Settings load();
    static void save(const Settings& settings);
};

} // namespace bluffskill::app_config
