#include "bluffskill/poker/house.hpp"

#include <QApplication>
#include <QDateTime>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTcpServer>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

class ServerWindow final : public QMainWindow {
public:
    ServerWindow() {
        setWindowTitle("BluffSkill Server");
        resize(1180, 600);
        auto* splitter = new QSplitter(this);
        tree_ = new QTreeWidget(splitter);
        tree_->setHeaderLabels({"House / competition / table / player"});
        log_ = new QPlainTextEdit(splitter);
        log_->setReadOnly(true);
        actionLog_ = new QTableWidget(splitter);
        actionLog_->setColumnCount(4);
        actionLog_->setHorizontalHeaderLabels({"Player", "Round", "Action", "Value"});
        actionLog_->horizontalHeader()->setStretchLastSection(true);
        actionLog_->verticalHeader()->setVisible(false);
        actionLog_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        actionLog_->setSelectionMode(QAbstractItemView::NoSelection);
        actionLog_->setAlternatingRowColors(true);
        splitter->setSizes({300, 380, 500});
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
        refreshActionLog();
    }

    bluffskill::poker::House& house() noexcept { return house_; }

private:
    void refreshActionLog() {
        actionLog_->setRowCount(0);
        for (const auto& competition : house_.competitions()) {
            for (const auto& table : competition.tables) {
                const auto view = house_.tableView(competition.name, table.name);
                for (const auto& action : view.actionHistory) {
                    const auto row = actionLog_->rowCount();
                    actionLog_->insertRow(row);
                    actionLog_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(action.player)));
                    actionLog_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(bluffskill::poker::toString(action.street))));
                    actionLog_->setItem(row, 2, new QTableWidgetItem(QString::fromUtf8(bluffskill::poker::toString(action.action))));
                    actionLog_->setItem(row, 3, new QTableWidgetItem(action.amount == 0 ? QString{} : QLocale().toString(action.amount)));
                }
            }
        }
    }

    bluffskill::poker::House house_;
    QTreeWidget* tree_{};
    QPlainTextEdit* log_{};
    QTableWidget* actionLog_{};
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

QJsonObject tableViewJson(const bluffskill::poker::TableView& table) {
    QJsonArray players;
    for (const auto& player : table.players) {
        QJsonArray holeCards;
        for (const auto card : player.holeCards) holeCards.append(QString::fromStdString(bluffskill::cards::toString(card)));
        players.append(QJsonObject{{"name", QString::fromStdString(player.name)},
            {"kind", QString::fromUtf8(bluffskill::poker::toString(player.kind))},
            {"seat", static_cast<int>(player.seat)}, {"stack", static_cast<qint64>(player.stack)},
            {"committed", static_cast<qint64>(player.committed)}, {"folded", player.folded},
            {"dealer", player.dealer}, {"acting", player.acting}, {"holeCards", holeCards}});
    }
    QJsonArray communityCards;
    for (const auto card : table.communityCards) communityCards.append(QString::fromStdString(bluffskill::cards::toString(card)));
    QJsonArray pots;
    for (const auto& pot : table.pots) {
        QJsonArray eligibleSeats;
        for (const auto seat : pot.eligibleSeats) eligibleSeats.append(static_cast<int>(seat));
        pots.append(QJsonObject{{"amount", static_cast<qint64>(pot.amount)}, {"eligibleSeats", eligibleSeats}});
    }
    QJsonArray payouts;
    for (const auto& payout : table.payouts) {
        QJsonArray awards;
        for (const auto& award : payout.awards) {
            awards.append(QJsonObject{{"seat", static_cast<int>(award.seat)}, {"amount", static_cast<qint64>(award.amount)}});
        }
        payouts.append(QJsonObject{{"amount", static_cast<qint64>(payout.amount)}, {"awards", awards}});
    }
    QJsonArray history;
    for (const auto& action : table.actionHistory) {
        history.append(QJsonObject{{"player", QString::fromStdString(action.player)}, {"seat", static_cast<int>(action.seat)},
            {"street", QString::fromUtf8(bluffskill::poker::toString(action.street))},
            {"action", QString::fromUtf8(bluffskill::poker::toString(action.action))}, {"amount", static_cast<qint64>(action.amount)}});
    }
    QJsonObject legalActions;
    if (table.legalActions) {
        const auto& legal = *table.legalActions;
        legalActions = {{"check", legal.check}, {"call", legal.call}, {"bet", legal.bet}, {"raise", legal.raise}, {"fold", legal.fold},
            {"callAmount", static_cast<qint64>(legal.callAmount)}, {"minimumAmount", static_cast<qint64>(legal.minimumAmount)},
            {"maximumAmount", static_cast<qint64>(legal.maximumAmount)}};
    }
    return {{"name", QString::fromStdString(table.name)}, {"sequence", static_cast<qint64>(table.eventSequence)},
        {"street", QString::fromUtf8(bluffskill::poker::toString(table.street))},
        {"currentBet", static_cast<qint64>(table.currentBet)},
        {"dealerSeat", table.dealerSeat ? static_cast<int>(*table.dealerSeat) : 0},
        {"smallBlindSeat", table.smallBlindSeat ? static_cast<int>(*table.smallBlindSeat) : 0},
        {"bigBlindSeat", table.bigBlindSeat ? static_cast<int>(*table.bigBlindSeat) : 0},
        {"actingSeat", table.actingSeat ? static_cast<int>(*table.actingSeat) : 0}, {"communityCards", communityCards},
        {"players", players}, {"pots", pots}, {"payouts", payouts}, {"showdownOccurred", table.showdownOccurred},
        {"actionHistory", history}, {"legalActions", legalActions}};
}

QHttpServerResponse tableViewResponse(const bluffskill::poker::TableView& table, QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::Ok) {
    QHttpServerResponse response(tableViewJson(table), status);
    auto headers = response.headers();
    headers.append("X-BluffSkill-Sequence", QString::number(table.eventSequence));
    response.setHeaders(std::move(headers));
    return response;
}

std::optional<bluffskill::poker::Action> parseAction(const QString& value) {
    const auto action = value.toLower();
    if (action == "check") return bluffskill::poker::Action::check;
    if (action == "call") return bluffskill::poker::Action::call;
    if (action == "bet") return bluffskill::poker::Action::bet;
    if (action == "raise") return bluffskill::poker::Action::raise;
    if (action == "fold") return bluffskill::poker::Action::fold;
    return std::nullopt;
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

    server.route("/v1/competitions/<arg>/tables/<arg>/view", [&window](const QString& competitionName, const QString& tableName,
        const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto viewerName = QUrlQuery(request.url()).queryItemValue("viewer");
            try {
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.log("GET /v1/competitions/" + competitionName + "/tables/" + tableName + "/view → 200");
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                window.log("GET /v1/competitions/" + competitionName + "/tables/" + tableName + "/view → 404");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::NotFound);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/actions", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto json = QJsonDocument::fromJson(request.body()).object();
            const auto action = parseAction(json.value("action").toString());
            const auto playerName = json.value("player").toString();
            const auto sequenceValue = json.value("expectedSequence").toDouble(-1);
            const auto amountValue = json.value("amount").toDouble(0);
            const auto invalidNumber = !json.value("expectedSequence").isDouble() || sequenceValue < 0
                || static_cast<double>(static_cast<std::uint64_t>(sequenceValue)) != sequenceValue
                || (json.contains("amount") && (!json.value("amount").isDouble() || amountValue < 0
                    || static_cast<double>(static_cast<bluffskill::poker::Chips>(amountValue)) != amountValue));
            if (json.isEmpty() || !action || playerName.isEmpty() || invalidNumber) {
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 400");
                return QHttpServerResponse(QJsonObject{{"error", "action, player, expectedSequence, and a non-negative integer amount are required"}}, QHttpServerResponder::StatusCode::BadRequest);
            }
            const auto amount = static_cast<bluffskill::poker::Chips>(amountValue);
            const auto sequence = static_cast<std::uint64_t>(sequenceValue);
            try {
                window.house().submitAction(competitionName.toStdString(), tableName.toStdString(), playerName.toStdString(), *action, amount, sequence);
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), playerName.toStdString());
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 200");
                return tableViewResponse(table);
            } catch (const bluffskill::poker::CommandError& exception) {
                const auto status = exception.failure() == bluffskill::poker::CommandFailure::illegalAction
                    ? QHttpServerResponder::StatusCode::UnprocessableEntity : QHttpServerResponder::StatusCode::Conflict;
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → " + (status == QHttpServerResponder::StatusCode::Conflict ? "409" : "422"));
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, status);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 400");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/next-hand", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            try {
                const auto viewerName = QJsonDocument::fromJson(request.body()).object().value("viewer").toString();
                window.house().startNextHand(competitionName.toStdString(), tableName.toStdString());
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/next-hand → 200");
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/next-hand → 409");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/players", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto json = QJsonDocument::fromJson(request.body()).object();
            try {
                const auto competition = window.house().createApiPlayer(
                    competitionName.toStdString(), tableName.toStdString(), json.value("name").toString().toStdString());
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/players → 201");
                return QHttpServerResponse(competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/players → 400");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
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
