#include "bluffskill/poker/house.hpp"

#include <QApplication>
#include <QDateTime>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTcpServer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

class ServerWindow final : public QMainWindow {
public:
    ServerWindow() {
        setWindowTitle("BluffSkill Server");
        resize(900, 600);
        auto* splitter = new QSplitter(this);
        tree_ = new QTreeWidget(splitter);
        tree_->setHeaderLabels({"House / competition / table / player"});
        log_ = new QPlainTextEdit(splitter);
        log_->setReadOnly(true);
        setCentralWidget(splitter);
        refreshTree();
    }

    void log(const QString& message) {
        log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + message);
    }

    void refreshTree() {
        tree_->clear();
        auto* house = new QTreeWidgetItem(tree_, {"House"});
        for (const auto& competition : house_.competitions()) {
            auto* competitionItem = new QTreeWidgetItem(house, {QString::fromStdString(competition.name)});
            for (const auto& table : competition.tables) {
                auto* tableItem = new QTreeWidgetItem(competitionItem, {QString::fromStdString(table.name) + " (" + QString::number(table.seats) + " / " + QString::number(competition.tournament.maximumPlayers) + ")"});
                for (const auto& player : competition.players) {
                    new QTreeWidgetItem(tableItem, {QString::fromStdString(player.name) + " (" + QString::fromUtf8(bluffskill::poker::toString(player.kind)) + ")"});
                }
            }
        }
        tree_->expandAll();
    }

    bluffskill::poker::House& house() noexcept { return house_; }

private:
    bluffskill::poker::House house_;
    QTreeWidget* tree_{};
    QPlainTextEdit* log_{};
};

QJsonObject competitionJson(const bluffskill::poker::CompetitionSummary& competition) {
    QJsonArray players;
    for (const auto& player : competition.players) {
        players.append(QJsonObject{{"name", QString::fromStdString(player.name)}, {"kind", QString::fromUtf8(bluffskill::poker::toString(player.kind))}});
    }
    return {{"name", QString::fromStdString(competition.name)},
            {"style", "Tournament"},
            {"table", QString::fromStdString(competition.tables.front().name)},
            {"maximumPlayers", static_cast<int>(competition.tournament.maximumPlayers)},
            {"startingStack", static_cast<int>(competition.tournament.startingStack)},
            {"referencePlayers", players}};
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    ServerWindow window;
    QHttpServer server;

    server.route("/v1/health", [&window] {
        window.log("GET /v1/health → 200");
        return QHttpServerResponse(QJsonObject{{"status", "ok"}});
    });

    server.route("/v1/competitions", QHttpServerRequest::Method::Post,
        [&window](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto json = QJsonDocument::fromJson(request.body()).object();
            try {
                const auto competition = window.house().createSingleTableTournament({
                    .maximumPlayers = static_cast<std::size_t>(json.value("maximumPlayers").toInt(8)),
                    .startingStack = static_cast<unsigned int>(json.value("startingStack").toInt(7000)),
                });
                window.refreshTree();
                window.log("POST /v1/competitions → 201 (" + QString::fromStdString(competition.name) + ")");
                return QHttpServerResponse(competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions → 400");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    server.route("/v1/competitions", [&window] {
        QJsonArray competitions;
        for (const auto& competition : window.house().competitions()) competitions.append(competitionJson(competition));
        window.log("GET /v1/competitions → 200");
        return QHttpServerResponse(competitions);
    });

    server.route("/v1/competitions/<arg>/reference-players", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto json = QJsonDocument::fromJson(request.body()).object();
            try {
                const auto competition = window.house().createReferencePlayers(
                    competitionName.toStdString(), static_cast<std::size_t>(json.value("count").toInt()));
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/reference-players → 201");
                return QHttpServerResponse(competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/reference-players → 400");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    QTcpServer tcpServer;
    if (!tcpServer.listen(QHostAddress::LocalHost, 0) || !server.bind(&tcpServer)) return 1;
    const auto port = tcpServer.serverPort();
    window.log("Listening on http://127.0.0.1:" + QString::number(port));
    window.log("Prototype REST endpoints: GET/POST /v1/competitions");
    window.show();
    return application.exec();
}
