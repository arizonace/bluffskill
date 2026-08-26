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
                auto* tableItem = new QTreeWidgetItem(competitionItem, {QString::fromStdString(table.name) + " (" + QString::number(table.players.size()) + " / " + QString::number(table.maximumSeats) + ")"});
                for (const auto& seatedPlayer : table.players) {
                    new QTreeWidgetItem(tableItem, {QString::number(seatedPlayer.seat) + ": " + QString::fromStdString(seatedPlayer.player.name) + " (" + QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind)) + ")"});
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
    for (const auto& table : competition.tables) {
        for (const auto& seatedPlayer : table.players) {
            players.append(QJsonObject{{"name", QString::fromStdString(seatedPlayer.player.name)},
                                       {"kind", QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind))},
                                       {"table", QString::fromStdString(table.name)},
                                       {"seat", static_cast<int>(seatedPlayer.seat)}});
        }
    }
    return {{"name", QString::fromStdString(competition.name)},
            {"style", "Tournament"},
            {"table", QString::fromStdString(competition.tables.front().name)},
            {"maximumPlayers", static_cast<int>(competition.tournament.maximumPlayers)},
            {"startingStack", static_cast<int>(competition.tournament.startingStack)},
            {"referencePlayers", players}};
}

QJsonObject tableJson(const bluffskill::poker::TableSummary& table) {
    QJsonArray players;
    for (const auto& seatedPlayer : table.players) {
        players.append(QJsonObject{{"name", QString::fromStdString(seatedPlayer.player.name)},
                                   {"kind", QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind))},
                                   {"seat", static_cast<int>(seatedPlayer.seat)}});
    }
    return {{"name", QString::fromStdString(table.name)},
            {"maximumSeats", static_cast<int>(table.maximumSeats)},
            {"players", players}};
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

    server.route("/v1/competitions/<arg>/tables", [&window](const QString& competitionName) -> QHttpServerResponse {
        const auto competition = window.house().competition(competitionName.toStdString());
        if (!competition) {
            window.log("GET /v1/competitions/" + competitionName + "/tables → 404");
            return QHttpServerResponse(QJsonObject{{"error", "competition was not found"}}, QHttpServerResponder::StatusCode::NotFound);
        }
        QJsonArray tables;
        for (const auto& table : competition->tables) tables.append(tableJson(table));
        window.log("GET /v1/competitions/" + competitionName + "/tables → 200");
        return QHttpServerResponse(tables);
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
