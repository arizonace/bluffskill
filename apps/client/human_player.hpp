#pragma once

#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace bluffskill::client {

// A local, human-operated controller. Its attach request intentionally contains no human-specific data.
class HumanPlayer final {
public:
    explicit HumanPlayer(QString apiPlayerName);

    [[nodiscard]] const QString& apiPlayerName() const noexcept;
    [[nodiscard]] QNetworkReply* attachToTable(
        QNetworkAccessManager& network,
        const QUrl& serverUrl,
        const QString& competitionName,
        const QString& tableName) const;

private:
    QString apiPlayerName_;
};

} // namespace bluffskill::client
