#include "human_player.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

namespace bluffskill::client {

HumanPlayer::HumanPlayer(QString apiPlayerName) : apiPlayerName_(std::move(apiPlayerName)) {}

const QString& HumanPlayer::apiPlayerName() const noexcept { return apiPlayerName_; }

void HumanPlayer::selectAction(HumanAction action) noexcept { selectedAction_ = action; }

std::optional<HumanAction> HumanPlayer::selectedAction() const noexcept { return selectedAction_; }

QString HumanPlayer::displayName(HumanAction action) {
    switch (action) {
    case HumanAction::check: return "Check";
    case HumanAction::call: return "Call";
    case HumanAction::bet: return "Bet";
    case HumanAction::fold: return "Fold";
    }
    return "Unknown";
}

QNetworkReply* HumanPlayer::attachToTable(
    QNetworkAccessManager& network,
    const QUrl& serverUrl,
    const QString& competitionName,
    const QString& tableName) const {
    auto endpoint = serverUrl;
    endpoint.setPath("/v1/competitions/" + competitionName + "/tables/" + tableName + "/players");
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return network.post(request, QJsonDocument(QJsonObject{{"name", apiPlayerName_}}).toJson(QJsonDocument::Compact));
}

} // namespace bluffskill::client
