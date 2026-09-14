#pragma once

#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace bluffskill::client {

enum class HumanAction { check, call, bet, raise, fold };

// A local, human-operated controller. Its attach request identifies only an
// API player and may carry local action-log card consent.
class HumanPlayer final {
public:
    explicit HumanPlayer(QString apiPlayerName, bool recordHoleCardsWhenFolding = false);

    [[nodiscard]] const QString& apiPlayerName() const noexcept;
    void selectAction(HumanAction action) noexcept;
    [[nodiscard]] std::optional<HumanAction> selectedAction() const noexcept;
    [[nodiscard]] static QString displayName(HumanAction action);
    [[nodiscard]] QNetworkReply* attachToTable(
        QNetworkAccessManager& network,
        const QUrl& serverUrl,
        const QString& competitionName,
        const QString& tableName) const;
    [[nodiscard]] QNetworkReply* submitAction(
        QNetworkAccessManager& network,
        const QUrl& serverUrl,
        const QString& competitionName,
        const QString& tableName,
        HumanAction action,
        qint64 amount,
        qint64 expectedSequence) const;

private:
    QString apiPlayerName_;
    bool recordHoleCardsWhenFolding_{false};
    std::optional<HumanAction> selectedAction_;
};

} // namespace bluffskill::client
