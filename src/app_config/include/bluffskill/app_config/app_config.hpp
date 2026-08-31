#pragma once

#include <QList>
#include <QString>

namespace bluffskill::app_config {

struct Settings {
    int playerClockSeconds{120};
    int dealClockSeconds{20};
    int uninterruptedDealerDelayMilliseconds{5'000};
    int automatedPlayerDelayMilliseconds{1'000};
    int smallBlind{1}; // Number of smallest chip.
    int blindHandsPerLevel{16};
    int blindMinutesPerLevel{20};
    QList<qint64> chipDenominations{25, 100, 500, 1000};
    QString defaultPlayerName{"Player"};
    QList<quint16> serverPreferredPorts{53153, 53154, 53155};
    bool clientAutoConnect{false};
};

class AppConfig final {
public:
    [[nodiscard]] static QString filePath();
    [[nodiscard]] static Settings load();
    static void save(const Settings& settings);
};

} // namespace bluffskill::app_config
