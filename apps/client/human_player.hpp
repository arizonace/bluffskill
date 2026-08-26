#pragma once

#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace bluffskill::client {

enum class HumanAction { check, call, bet, fold };

// A local, human-operated controller. Its attach request intentionally contains no human-specific data.
class HumanPlayer final {
public:
    explicit HumanPlayer(QString apiPlayerName);

    [[nodiscard]] const QString& apiPlayerName() const noexcept;
    void selectAction(HumanAction action) noexcept;
    [[nodiscard]] std::optional<HumanAction> selectedAction() const noexcept;
    [[nodiscard]] static QString displayName(HumanAction action);
    [[nodiscard]] QNetworkReply* attachToTable(
        QNetworkAccessManager& network,
        const QUrl& serverUrl,
        const QString& competitionName,
        const QString& tableName) const;

private:
    QString apiPlayerName_;
    std::optional<HumanAction> selectedAction_;
};

} // namespace bluffskill::client
