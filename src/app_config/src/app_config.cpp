#include "bluffskill/app_config/app_config.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace bluffskill::app_config {

namespace {

constexpr int defaultPlayerClockSeconds = 120;
constexpr int defaultDealClockSeconds = 20;
constexpr quint16 defaultPorts[] = {53153, 53154, 53155};

Settings normalized(Settings settings) {
    settings.playerClockSeconds = std::clamp(settings.playerClockSeconds, 5, 3'600);
    settings.dealClockSeconds = std::clamp(settings.dealClockSeconds, 1, 3'600);
    settings.defaultPlayerName = settings.defaultPlayerName.trimmed();
    if (settings.defaultPlayerName.isEmpty()) settings.defaultPlayerName = "Player";

    QList<quint16> ports;
    for (const auto port : settings.serverPreferredPorts) {
        if (port != 0 && !ports.contains(port)) ports.append(port);
        if (ports.size() == 3) break;
    }
    for (const auto port : defaultPorts) {
        if (ports.size() == 3) break;
        if (!ports.contains(port)) ports.append(port);
    }
    settings.serverPreferredPorts = ports;
    return settings;
}

} // namespace

QString AppConfig::filePath() {
    return QDir::homePath() + "/.config/azonelayer/blindskill/blindskill.conf";
}

Settings AppConfig::load() {
    const auto path = filePath();
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QSettings store(path, QSettings::IniFormat);
    Settings settings;
    settings.playerClockSeconds = store.value("timers/playerClockSeconds", defaultPlayerClockSeconds).toInt();
    settings.dealClockSeconds = store.value("timers/dealClockSeconds", defaultDealClockSeconds).toInt();
    settings.defaultPlayerName = store.value("client/defaultPlayerName", settings.defaultPlayerName).toString();
    settings.clientAutoConnect = store.value("client/autoConnect", false).toBool();
    settings.serverPreferredPorts.clear();
    for (const auto& value : store.value("server/preferredPorts").toStringList()) {
        bool valid = false;
        const auto port = value.toUShort(&valid);
        if (valid && port != 0) settings.serverPreferredPorts.append(port);
    }
    settings = normalized(std::move(settings));
    save(settings); // Establishes the shared file, including defaults, on first use.
    return settings;
}

void AppConfig::save(const Settings& input) {
    const auto settings = normalized(input);
    const auto path = filePath();
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QSettings store(path, QSettings::IniFormat);
    store.setValue("timers/playerClockSeconds", settings.playerClockSeconds);
    store.setValue("timers/dealClockSeconds", settings.dealClockSeconds);
    store.setValue("client/defaultPlayerName", settings.defaultPlayerName);
    store.setValue("client/autoConnect", settings.clientAutoConnect);
    QStringList ports;
    for (const auto port : settings.serverPreferredPorts) ports.append(QString::number(port));
    store.setValue("server/preferredPorts", ports);
    store.sync();
}

} // namespace bluffskill::app_config
