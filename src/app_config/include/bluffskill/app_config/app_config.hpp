#pragma once

#include <QList>
#include <QString>

namespace bluffskill::app_config {

struct Settings {
    int playerClockSeconds{120};
    int dealClockSeconds{20};
    int blindHandsPerLevel{16};
    int blindMinutesPerLevel{20};
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
