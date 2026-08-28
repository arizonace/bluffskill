#include "bluffskill/app_config/app_config.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>

#include <algorithm>

namespace bluffskill::app_config {

namespace {

constexpr int defaultPlayerClockSeconds = 120;
constexpr int defaultDealClockSeconds = 20;
constexpr int defaultUninterruptedDealerDelayMilliseconds = 5'000;
constexpr int defaultAutomatedPlayerDelayMilliseconds = 1'000;
constexpr int defaultBlindHandsPerLevel = 16;
constexpr int defaultBlindMinutesPerLevel = 20;
constexpr quint16 defaultPorts[] = {53153, 53154, 53155};
constexpr qint64 defaultChipDenominations[] = {25, 100, 500, 1000};

Settings normalized(Settings settings) {
    settings.playerClockSeconds = std::clamp(settings.playerClockSeconds, 5, 3'600);
    settings.dealClockSeconds = std::clamp(settings.dealClockSeconds, 1, 3'600);
    settings.uninterruptedDealerDelayMilliseconds = std::clamp(settings.uninterruptedDealerDelayMilliseconds, 1, 3'600'000);
    settings.automatedPlayerDelayMilliseconds = std::clamp(settings.automatedPlayerDelayMilliseconds, 1, 3'600'000);
    settings.blindHandsPerLevel = std::clamp(settings.blindHandsPerLevel, 1, 10'000);
    settings.blindMinutesPerLevel = std::clamp(settings.blindMinutesPerLevel, 1, 3'600);
    QList<qint64> denominations;
    for (const auto denomination : settings.chipDenominations) {
        if (denomination > 0 && !denominations.contains(denomination)) denominations.append(denomination);
    }
    std::sort(denominations.begin(), denominations.end());
    if (denominations.isEmpty()) {
        for (const auto denomination : defaultChipDenominations) denominations.append(denomination);
    }
    settings.chipDenominations = denominations;
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
    settings.uninterruptedDealerDelayMilliseconds = store.value("timers/uninterruptedDealerDelayMilliseconds", defaultUninterruptedDealerDelayMilliseconds).toInt();
    settings.automatedPlayerDelayMilliseconds = store.value("timers/automatedPlayerDelayMilliseconds", defaultAutomatedPlayerDelayMilliseconds).toInt();
    settings.blindHandsPerLevel = store.value("blinds/handsPerLevel", defaultBlindHandsPerLevel).toInt();
    settings.blindMinutesPerLevel = store.value("blinds/minutesPerLevel", defaultBlindMinutesPerLevel).toInt();
    settings.chipDenominations.clear();
    const auto denominationText = store.value("chips/denominations").toStringList().join(',');
    for (const auto& token : denominationText.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)) {
        bool valid = false;
        const auto denomination = token.toLongLong(&valid);
        if (valid && denomination > 0) settings.chipDenominations.append(denomination);
    }
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
    store.setValue("timers/uninterruptedDealerDelayMilliseconds", settings.uninterruptedDealerDelayMilliseconds);
    store.setValue("timers/automatedPlayerDelayMilliseconds", settings.automatedPlayerDelayMilliseconds);
    store.setValue("blinds/handsPerLevel", settings.blindHandsPerLevel);
    store.setValue("blinds/minutesPerLevel", settings.blindMinutesPerLevel);
    QStringList denominations;
    for (const auto denomination : settings.chipDenominations) denominations.append(QString::number(denomination));
    store.setValue("chips/denominations", denominations);
    store.setValue("client/defaultPlayerName", settings.defaultPlayerName);
    store.setValue("client/autoConnect", settings.clientAutoConnect);
    QStringList ports;
    for (const auto port : settings.serverPreferredPorts) ports.append(QString::number(port));
    store.setValue("server/preferredPorts", ports);
    store.sync();
}

} // namespace bluffskill::app_config
