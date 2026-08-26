#include "human_player.hpp"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QIntValidator>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QSet>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>
#include <QLineEdit>

#include <memory>
#include <vector>

namespace {

class PokerTable final : public QWidget {
public:
    explicit PokerTable(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(900, 520); }

    void setPlayers(const QJsonArray& players) {
        playerNames_.fill({});
        for (const auto& item : players) {
            const auto player = item.toObject();
            const auto seat = player.value("seat").toInt();
            if (seat >= 1 && seat <= static_cast<int>(playerNames_.size())) {
                playerNames_[static_cast<std::size_t>(seat - 1)] = player.value("name").toString();
            }
        }
        update();
    }

    void clearPlayers() {
        playerNames_.fill({});
        update();
    }

    void setLocalPlayerName(QString name) {
        localPlayerName_ = std::move(name);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#1D4A38"));
        const QRectF felt = rect().adjusted(55, 45, -55, -45);
        painter.setPen(QPen(QColor("#C8A55B"), 5));
        painter.setBrush(QColor("#256044"));
        painter.drawRoundedRect(felt, 170, 170);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Helvetica", 18, QFont::DemiBold));
        painter.drawText(felt, Qt::AlignCenter, "POT  0\n\nCommunity cards will appear here");
        constexpr std::array<QPointF, 8> seats{{{.25, .10}, {.50, .07}, {.75, .10}, {.93, .50}, {.75, .90}, {.50, .93}, {.25, .90}, {.07, .50}}};
        painter.setFont(QFont("Helvetica", 12));
        for (std::size_t i = 0; i < seats.size(); ++i) {
            const auto point = QPointF(width() * seats[i].x(), height() * seats[i].y());
            painter.setBrush(QColor("#162D24"));
            const auto localPlayer = !localPlayerName_.isEmpty() && playerNames_[i] == localPlayerName_;
            painter.setPen(QPen(localPlayer ? QColor("#F6D365") : Qt::white, localPlayer ? 3 : 1));
            painter.drawEllipse(point, 43, 43);
            const auto label = playerNames_[i].isEmpty()
                ? "Seat " + QString::number(i + 1)
                : playerNames_[i] + "\nSeat " + QString::number(i + 1);
            painter.drawText(QRectF(point.x() - 58, point.y() - 20, 116, 40), Qt::AlignCenter, label);
        }
    }

private:
    std::array<QString, 8> playerNames_{};
    QString localPlayerName_;
};

class ConnectionDialog final : public QDialog {
public:
    explicit ConnectionDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Connect to BluffSkill Server");
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        host_ = new QLineEdit("127.0.0.1", this);
        host_->setPlaceholderText("localhost or a server name");
        port_ = new QLineEdit(this);
        port_->setValidator(new QIntValidator(1, 65535, port_));
        port_->setPlaceholderText("Server port");
        form->addRow("Server", host_);
        form->addRow("Port", port_);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (host_->text().trimmed().isEmpty() || port_->text().isEmpty()) {
                QMessageBox::warning(this, "Connection details required", "Enter both a server name and its port.");
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    [[nodiscard]] QUrl healthUrl() const {
        return serverUrl("/v1/health");
    }

    [[nodiscard]] QUrl serverUrl(const QString& path = "/") const {
        QUrl url;
        url.setScheme("http"); // The prototype server is intentionally localhost HTTP only.
        url.setHost(host_->text().trimmed());
        url.setPort(port_->text().toInt());
        url.setPath(path);
        return url;
    }

    [[nodiscard]] QString displayAddress() const {
        return host_->text().trimmed() + ':' + port_->text();
    }

private:
    QLineEdit* host_{};
    QLineEdit* port_{};
};

class ClientWindow final : public QMainWindow {
public:
    ClientWindow() {
        setWindowTitle("BluffSkill");
        resize(1100, 760);
        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        auto* connection = new QFrame(central);
        auto* form = new QFormLayout(connection);
        server_ = new QComboBox(connection); server_->addItem("Not connected"); server_->setEnabled(false);
        competition_ = new QComboBox(connection); competition_->addItem("Choose a server first"); competition_->setEnabled(false);
        table_ = new QComboBox(connection); table_->addItem("Choose a competition first"); table_->setEnabled(false);
        form->addRow("Server", server_); form->addRow("Competition", competition_); form->addRow("Table", table_);
        layout->addWidget(connection);
        pokerTable_ = new PokerTable(central);
        layout->addWidget(pokerTable_, 1);
        auto* actions = new QFrame(central);
        auto* actionLayout = new QHBoxLayout(actions);
        actionStatus_ = new QLabel("No human player is attached.", actions);
        actionLayout->addWidget(actionStatus_);
        for (const auto* action : {"Check", "Call", "Bet", "Fold"}) {
            auto* button = new QPushButton(action, actions);
            button->setEnabled(false);
            actionButtons_.push_back(button);
            connect(button, &QPushButton::clicked, this, [this, action] { selectHumanAction(action); });
            actionLayout->addWidget(button);
        }
        layout->addWidget(actions);
        setCentralWidget(central);
        auto* connectionMenu = menuBar()->addMenu("Connection");
        auto* connectAction = connectionMenu->addAction("Connect…");
        disconnectAction_ = connectionMenu->addAction("Disconnect");
        disconnectAction_->setEnabled(false);
        connect(connectAction, &QAction::triggered, this, [this] { connectToServer(); });
        connect(disconnectAction_, &QAction::triggered, this, [this] { disconnectFromServer(); });
        auto* gameMenu = menuBar()->addMenu("Game");
        newGameAction_ = gameMenu->addAction("New Game…");
        newGameAction_->setEnabled(false);
        connect(newGameAction_, &QAction::triggered, this, [this] { newGame(); });
        connect(competition_, &QComboBox::currentIndexChanged, this, [this] { refreshTables(); });
        connect(table_, &QComboBox::currentIndexChanged, this, [this] { refreshSeats(); });
        statusBar()->showMessage("Choose Connection → Connect… to begin.");
    }

private:
    [[nodiscard]] QNetworkReply* track(QNetworkReply* reply) {
        activeReplies_.insert(reply);
        return reply;
    }

    void release(QNetworkReply* reply) {
        activeReplies_.remove(reply);
        reply->deleteLater();
    }

    [[nodiscard]] QUrl endpointUrl(const QString& path) const {
        auto url = serverUrl_;
        url.setPath(path);
        return url;
    }

    [[nodiscard]] QNetworkReply* postJson(const QString& path, const QJsonObject& body) {
        QNetworkRequest request(endpointUrl(path));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        return track(network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)));
    }

    void resetDisconnectedUi() {
        server_->clear();
        server_->addItem("Not connected");
        server_->setEnabled(false);
        competition_->clear();
        competition_->addItem("Choose a server first");
        competition_->setEnabled(false);
        table_->clear();
        table_->addItem("Choose a competition first");
        table_->setEnabled(false);
        clearHumanPlayer();
        disconnectAction_->setEnabled(false);
        newGameAction_->setEnabled(false);
    }

    void connectToServer() {
        ConnectionDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;

        const auto address = dialog.displayAddress();
        const auto attempt = ++connectionGeneration_;
        if (healthReply_) healthReply_->abort();
        newGameAction_->setEnabled(false);
        statusBar()->showMessage("Connecting to " + address + "…");
        auto* reply = track(network_.get(QNetworkRequest(dialog.healthUrl())));
        healthReply_ = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, address, baseUrl = dialog.serverUrl(), attempt] {
            if (healthReply_ == reply) healthReply_ = nullptr;
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = reply->readAll();
            const auto body = QJsonDocument::fromJson(response).object();
            const auto connected = reply->error() == QNetworkReply::NoError && status == 200
                && body.value("status") == "ok";
            const auto error = reply->errorString();
            release(reply);

            // A newer connect or disconnect request made this reply irrelevant.
            if (attempt != connectionGeneration_) return;

            if (!connected) {
                statusBar()->showMessage("Could not connect to " + address);
                const auto detail = status > 0 ? "The server returned HTTP " + QString::number(status) + "." : error;
                QMessageBox::warning(this, "Server unavailable",
                    "BluffSkill could not verify the server at " + address + ".\n\n" + detail);
                newGameAction_->setEnabled(connected_);
                return;
            }

            connected_ = true;
            serverUrl_ = baseUrl;
            clearHumanPlayer();
            server_->setEnabled(true);
            server_->clear();
            server_->addItem(address);
            disconnectAction_->setEnabled(true);
            newGameAction_->setEnabled(true);
            statusBar()->showMessage("Connected to " + address);
            refreshCompetitions();
        });
    }

    void disconnectFromServer() {
        ++connectionGeneration_;
        const auto replies = activeReplies_;
        for (auto* reply : replies) reply->abort();
        activeReplies_.clear();
        healthReply_ = nullptr;
        network_.clearAccessCache();
        network_.clearConnectionCache();
        connected_ = false;
        serverUrl_ = QUrl{};
        resetDisconnectedUi();
        statusBar()->showMessage("Disconnected.");
    }

    void refreshCompetitions(const QString& preferredName = {}) {
        if (!connected_) return;
        const auto attempt = connectionGeneration_;
        auto* reply = track(network_.get(QNetworkRequest(endpointUrl("/v1/competitions"))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, preferredName] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto body = QJsonDocument::fromJson(reply->readAll()).array();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                competition_->clear();
                competition_->addItem("Could not retrieve competitions");
                competition_->setEnabled(false);
                statusBar()->showMessage("Connected, but competitions could not be retrieved.");
                return;
            }

            {
                QSignalBlocker blocker(competition_);
                competition_->clear();
                for (const auto& item : body) {
                    const auto competition = item.toObject();
                    competition_->addItem(competition.value("name").toString(), competition);
                }
            }
            if (competition_->count() == 0) {
                competition_->addItem("No competitions yet");
                competition_->setEnabled(false);
                {
                    QSignalBlocker blocker(table_);
                    table_->clear();
                    table_->addItem("Create a new game first");
                    table_->setEnabled(false);
                }
                pokerTable_->clearPlayers();
                return;
            }
            competition_->setEnabled(true);
            if (!preferredName.isEmpty()) {
                const auto index = competition_->findText(preferredName);
                if (index >= 0) {
                    QSignalBlocker blocker(competition_);
                    competition_->setCurrentIndex(index);
                }
            }
            refreshTables();
        });
    }

    void refreshTables() {
        if (!connected_ || competition_->currentIndex() < 0) return;
        const auto competitionName = competition_->currentText();
        if (competitionName.isEmpty()) return;
        const auto attempt = connectionGeneration_;
        const auto tableRequest = ++tableRequestGeneration_;
        const auto path = "/v1/competitions/" + competitionName + "/tables";
        auto* reply = track(network_.get(QNetworkRequest(endpointUrl(path))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, tableRequest] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto tables = QJsonDocument::fromJson(reply->readAll()).array();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || tableRequest != tableRequestGeneration_ || !connected_) return;
            if (!success) {
                QSignalBlocker blocker(table_);
                table_->clear();
                table_->addItem("Could not retrieve tables");
                table_->setEnabled(false);
                pokerTable_->clearPlayers();
                return;
            }
            {
                QSignalBlocker blocker(table_);
                table_->clear();
                for (const auto& item : tables) {
                    const auto table = item.toObject();
                    table_->addItem(table.value("name").toString(), table);
                }
                table_->setEnabled(table_->count() > 0);
            }
            refreshSeats();
        });
    }

    void refreshSeats() {
        if (table_->currentIndex() < 0) {
            pokerTable_->clearPlayers();
            return;
        }
        const auto table = table_->currentData().toJsonObject();
        pokerTable_->setPlayers(table.value("players").toArray());
    }

    void newGame() {
        if (!connected_) return;
        bool accepted = false;
        const auto playerName = QInputDialog::getText(this, "Your player", "Player name:", QLineEdit::Normal, "Player", &accepted).trimmed();
        if (!accepted) return;
        if (playerName.isEmpty()) {
            QMessageBox::warning(this, "Player name required", "Choose a name using letters, digits, and dashes.");
            return;
        }
        const auto attempt = connectionGeneration_;
        pendingHumanPlayer_ = std::make_unique<bluffskill::client::HumanPlayer>(playerName);
        newGameAction_->setEnabled(false);
        statusBar()->showMessage("Creating a six-player reference tournament…");
        auto* reply = postJson("/v1/competitions", QJsonObject{
            {"flavor", "NoLimitTexasHoldEm"},
            {"maximumPlayers", 8},
            {"startingStack", 7000},
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto competition = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "New game failed", "The server could not create a new competition.");
                pendingHumanPlayer_.reset();
                newGameAction_->setEnabled(true);
                return;
            }
            addReferencePlayers(competition.value("name").toString(), attempt);
        });
    }

    void addReferencePlayers(const QString& competitionName, std::uint64_t attempt) {
        const auto path = "/v1/competitions/" + competitionName + "/reference-players";
        auto* reply = postJson(path, QJsonObject{{"count", 6}});
        connect(reply, &QNetworkReply::finished, this, [this, reply, competitionName, attempt] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "Reference players failed", "The competition was created, but its six reference players were not added.");
                pendingHumanPlayer_.reset();
                newGameAction_->setEnabled(true);
                refreshCompetitions(competitionName);
                return;
            }
            attachHumanPlayer(competitionName, "Red", attempt);
        });
    }

    void attachHumanPlayer(const QString& competitionName, const QString& tableName, std::uint64_t attempt) {
        if (!pendingHumanPlayer_) return;
        auto* reply = track(pendingHumanPlayer_->attachToTable(network_, serverUrl_, competitionName, tableName));
        connect(reply, &QNetworkReply::finished, this, [this, reply, competitionName, attempt] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            newGameAction_->setEnabled(true);
            if (!success) {
                const auto detail = response.value("error").toString("The server could not attach your player.");
                QMessageBox::warning(this, "Player attachment failed", detail);
                pendingHumanPlayer_.reset();
                refreshCompetitions(competitionName);
                return;
            }
            humanPlayer_ = std::move(pendingHumanPlayer_);
            pokerTable_->setLocalPlayerName(humanPlayer_->apiPlayerName());
            setActionControlsEnabled(true);
            actionStatus_->setText("You are " + humanPlayer_->apiPlayerName() + ". Choose an action when it is your turn.");
            statusBar()->showMessage("Created " + competitionName + " with six reference players and " + humanPlayer_->apiPlayerName() + ".");
            refreshCompetitions(competitionName);
        });
    }

    void selectHumanAction(const char* actionName) {
        if (!humanPlayer_) return;
        const auto action = QString::fromUtf8(actionName);
        const auto humanAction = action == "Check" ? bluffskill::client::HumanAction::check
            : action == "Call" ? bluffskill::client::HumanAction::call
            : action == "Bet" ? bluffskill::client::HumanAction::bet
            : bluffskill::client::HumanAction::fold;
        humanPlayer_->selectAction(humanAction);
        actionStatus_->setText(humanPlayer_->apiPlayerName() + " selected " + bluffskill::client::HumanPlayer::displayName(humanAction) + ".");
        statusBar()->showMessage("Action selected locally; it will be submitted when the server provides a legal action turn.");
    }

    void setActionControlsEnabled(bool enabled) {
        for (auto* button : actionButtons_) button->setEnabled(enabled);
    }

    void clearHumanPlayer() {
        pendingHumanPlayer_.reset();
        humanPlayer_.reset();
        pokerTable_->setLocalPlayerName({});
        setActionControlsEnabled(false);
        actionStatus_->setText("No human player is attached.");
    }

private:
    QNetworkAccessManager network_{this};
    QSet<QNetworkReply*> activeReplies_;
    QNetworkReply* healthReply_{};
    QUrl serverUrl_;
    std::uint64_t connectionGeneration_{};
    std::uint64_t tableRequestGeneration_{};
    bool connected_{};
    QComboBox* server_{};
    QComboBox* competition_{};
    QComboBox* table_{};
    PokerTable* pokerTable_{};
    QLabel* actionStatus_{};
    std::vector<QPushButton*> actionButtons_;
    QAction* disconnectAction_{};
    QAction* newGameAction_{};
    std::unique_ptr<bluffskill::client::HumanPlayer> pendingHumanPlayer_;
    std::unique_ptr<bluffskill::client::HumanPlayer> humanPlayer_;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    ClientWindow window;
    window.show();
    return application.exec();
}
