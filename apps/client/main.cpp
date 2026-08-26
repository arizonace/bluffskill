#include "human_player.hpp"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
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
#include <QSpinBox>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLocale>

#include <memory>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

class PokerTable final : public QWidget {
public:
    explicit PokerTable(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(900, 520);
        setAccessibleName("Poker table");
    }

    void setPlayers(const QJsonArray& players) {
        playerNames_.fill({});
        playerStacks_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
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
        playerStacks_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
        pot_ = 0;
        currentBet_ = 0;
        street_.clear();
        communityCards_.clear();
        localHoleCards_.clear();
        update();
    }

    void setTableView(const QJsonObject& table) {
        playerNames_.fill({});
        playerStacks_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
        for (const auto& item : table.value("players").toArray()) {
            const auto player = item.toObject();
            const auto seat = player.value("seat").toInt();
            if (seat >= 1 && seat <= static_cast<int>(playerNames_.size())) {
                const auto index = static_cast<std::size_t>(seat - 1);
                playerNames_[index] = player.value("name").toString();
                playerStacks_[index] = player.value("stack").toInteger();
                playerActing_[index] = player.value("acting").toBool();
                playerDealer_[index] = player.value("dealer").toBool();
            }
        }
        pot_ = 0;
        for (const auto& item : table.value("pots").toArray()) pot_ += item.toObject().value("amount").toInteger();
        currentBet_ = table.value("currentBet").toInteger();
        street_ = table.value("street").toString();
        communityCards_.clear();
        for (const auto& card : table.value("communityCards").toArray()) communityCards_.append(card.toString());
        localHoleCards_.clear();
        if (!localPlayerName_.isEmpty()) {
            for (const auto& item : table.value("players").toArray()) {
                const auto player = item.toObject();
                if (player.value("name").toString() != localPlayerName_) continue;
                for (const auto& card : player.value("holeCards").toArray()) localHoleCards_.append(card.toString());
                break;
            }
        }
        update();
    }

    void setLocalPlayerName(QString name) {
        localPlayerName_ = std::move(name);
        localHoleCards_.clear();
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
        constexpr std::array<QPointF, 8> seats{{{.25, .10}, {.50, .07}, {.75, .10}, {.93, .50}, {.75, .90}, {.50, .93}, {.25, .90}, {.07, .50}}};
        QPointF localPoint;
        bool hasLocalSeat = false;
        painter.setFont(QFont("Helvetica", 12));
        for (std::size_t i = 0; i < seats.size(); ++i) {
            const auto point = QPointF(width() * seats[i].x(), height() * seats[i].y());
            painter.setBrush(QColor("#162D24"));
            const auto localPlayer = !localPlayerName_.isEmpty() && playerNames_[i] == localPlayerName_;
            if (localPlayer) {
                localPoint = point;
                hasLocalSeat = true;
            }
            painter.setPen(QPen(localPlayer ? QColor("#F6D365") : Qt::white, localPlayer ? 3 : 1));
            painter.drawEllipse(point, 43, 43);
            const auto label = playerNames_[i].isEmpty()
                ? "Seat " + QString::number(i + 1)
                : playerNames_[i] + "\n" + QString::number(playerStacks_[i]) + " chips"
                    + (playerDealer_[i] ? "  Dealer" : "") + (playerActing_[i] ? "  Acting" : "");
            painter.drawText(QRectF(point.x() - 58, point.y() - 20, 116, 40), Qt::AlignCenter, label);
        }

        drawBoard(painter, felt);
        if (hasLocalSeat && !localHoleCards_.isEmpty()) drawHoleCards(painter, felt, localPoint);
    }

private:
    struct CompactCard {
        QString rank;
        QString suit;
        QColor color;
    };

    [[nodiscard]] static QString chips(qint64 amount) { return QLocale().toString(amount); }

    [[nodiscard]] static CompactCard compactCard(const QString& description) {
        const auto pieces = description.split(' ', Qt::SkipEmptyParts);
        const auto rank = pieces.value(0, "?");
        const auto suitName = pieces.value(2).toLower();
        if (suitName == "clubs") return {rank, "♣", QColor("#172018")};
        if (suitName == "diamonds") return {rank, "♦", QColor("#C82B30")};
        if (suitName == "hearts") return {rank, "♥", QColor("#C82B30")};
        if (suitName == "spades") return {rank, "♠", QColor("#172018")};
        return {rank, "?", QColor("#172018")};
    }

    static void drawCard(QPainter& painter, const QRectF& cardRect, const QString& description) {
        const auto card = compactCard(description);
        painter.save();
        painter.setPen(QPen(QColor("#C8A55B"), 2));
        painter.setBrush(QColor("#FFF9EA"));
        painter.drawRoundedRect(cardRect, 7, 7);
        painter.setPen(card.color);
        painter.setFont(QFont("Helvetica", 16, QFont::Bold));
        painter.drawText(cardRect.adjusted(6, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, card.rank);
        painter.setFont(QFont("Helvetica", 34, QFont::DemiBold));
        painter.drawText(cardRect, Qt::AlignCenter, card.suit);
        painter.restore();
    }

    static void drawCardRow(QPainter& painter, const QStringList& cards, QPointF center, qreal width, qreal height) {
        constexpr qreal gap = 8;
        const auto totalWidth = cards.size() * width + std::max<qsizetype>(0, cards.size() - 1) * gap;
        auto left = center.x() - totalWidth / 2.0;
        for (const auto& card : cards) {
            drawCard(painter, {left, center.y() - height / 2.0, width, height}, card);
            left += width + gap;
        }
    }

    void drawBoard(QPainter& painter, const QRectF& felt) const {
        painter.save();
        painter.setPen(Qt::white);
        painter.setFont(QFont("Helvetica", 18, QFont::DemiBold));
        const auto header = "POT  " + chips(pot_) + "\n" + (street_.isEmpty() ? "Waiting" : street_)
            + "\nCurrent bet: " + chips(currentBet_);
        painter.drawText(QRectF(felt.center().x() - 200, felt.center().y() - 105, 400, 72), Qt::AlignCenter, header);
        if (communityCards_.isEmpty()) {
            painter.setFont(QFont("Helvetica", 13));
            painter.drawText(QRectF(felt.center().x() - 200, felt.center().y() - 18, 400, 30), Qt::AlignCenter, "Community cards will appear here");
        } else {
            drawCardRow(painter, communityCards_, {felt.center().x(), felt.center().y() + 22}, 54, 74);
        }
        painter.restore();
    }

    void drawHoleCards(QPainter& painter, const QRectF& felt, const QPointF& localPoint) const {
        const auto vector = localPoint - felt.center();
        const auto length = std::hypot(vector.x(), vector.y());
        const auto direction = length > 0.01 ? QPointF(vector.x() / length, vector.y() / length) : QPointF(0, 1);
        const auto cardCenter = localPoint - direction * 112;
        painter.save();
        painter.setPen(QColor("#F6D365"));
        painter.setFont(QFont("Helvetica", 12, QFont::DemiBold));
        painter.drawText(QRectF(cardCenter.x() - 90, cardCenter.y() - 52, 180, 20), Qt::AlignCenter, "Your hole cards");
        drawCardRow(painter, localHoleCards_, cardCenter, 46, 64);
        painter.restore();
    }

    std::array<QString, 8> playerNames_{};
    std::array<qint64, 8> playerStacks_{};
    std::array<bool, 8> playerActing_{};
    std::array<bool, 8> playerDealer_{};
    QString localPlayerName_;
    QStringList communityCards_;
    QStringList localHoleCards_;
    qint64 pot_{0};
    qint64 currentBet_{0};
    QString street_;
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
        auto* actionLayout = new QVBoxLayout(actions);
        auto* actionInfoLayout = new QHBoxLayout;
        actionStatus_ = new QLabel("No human player is attached.", actions);
        actionInfoLayout->addWidget(actionStatus_);
        wagerStatus_ = new QLabel("Current bet: 0 chips", actions);
        wagerStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        actionInfoLayout->addWidget(wagerStatus_, 1);
        actionLayout->addLayout(actionInfoLayout);
        auto* actionControlsLayout = new QHBoxLayout;
        amount_ = new QSpinBox(actions);
        amount_->setRange(0, 1000000000);
        amount_->setPrefix("Total commitment: ");
        amount_->setEnabled(false);
        actionControlsLayout->addWidget(amount_);
        for (const auto* action : {"Check", "Call", "Bet", "Raise", "Fold"}) {
            auto* button = new QPushButton(action, actions);
            button->setProperty("actionName", QString::fromUtf8(action).toLower());
            button->setEnabled(false);
            actionButtons_.push_back(button);
            connect(button, &QPushButton::clicked, this, [this, action] { selectHumanAction(action); });
            actionControlsLayout->addWidget(button);
        }
        actionLayout->addLayout(actionControlsLayout);
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

    [[nodiscard]] QUrl tableViewUrl() const {
        auto url = endpointUrl("/v1/competitions/" + competition_->currentText() + "/tables/" + table_->currentText() + "/view");
        QUrlQuery query;
        if (humanPlayer_) query.addQueryItem("viewer", humanPlayer_->apiPlayerName());
        url.setQuery(query);
        return url;
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
        refreshTableView();
    }

    void refreshTableView() {
        if (!connected_ || competition_->currentIndex() < 0 || table_->currentIndex() < 0) return;
        const auto attempt = connectionGeneration_;
        const auto requestGeneration = ++tableViewRequestGeneration_;
        auto* reply = track(network_.get(QNetworkRequest(tableViewUrl())));
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, requestGeneration] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto view = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || requestGeneration != tableViewRequestGeneration_ || !connected_) return;
            if (!success) {
                setActionControlsEnabled(false);
                actionStatus_->setText("Could not retrieve the current table state.");
                return;
            }
            applyTableView(view);
        });
    }

    void applyTableView(const QJsonObject& view) {
        pokerTable_->setTableView(view);
        tableSequence_ = view.value("sequence").toInteger();
        const auto legal = view.value("legalActions").toObject();
        const auto currentBet = view.value("currentBet").toInteger();
        const auto callAmount = legal.value("callAmount").toInteger();
        bool anyAction = false;
        for (auto* button : actionButtons_) {
            const auto actionName = button->property("actionName").toString();
            const auto enabled = humanPlayer_ && legal.value(actionName).toBool();
            button->setEnabled(enabled);
            button->setText(actionName == "call" && enabled && callAmount > 0
                ? "Call " + QLocale().toString(callAmount) : actionName.left(1).toUpper() + actionName.mid(1));
            anyAction = anyAction || enabled;
        }
        const auto minimum = legal.value("minimumAmount").toInteger();
        const auto maximum = legal.value("maximumAmount").toInteger();
        const auto canSetAmount = humanPlayer_ && (legal.value("bet").toBool() || legal.value("raise").toBool()) && maximum >= minimum;
        amount_->setEnabled(canSetAmount);
        if (canSetAmount) {
            amount_->setRange(static_cast<int>(std::min<qint64>(minimum, 1000000000)), static_cast<int>(std::min<qint64>(maximum, 1000000000)));
            amount_->setValue(static_cast<int>(std::min<qint64>(minimum, 1000000000)));
        }
        wagerStatus_->setText("Current bet: " + QLocale().toString(currentBet) + " chips"
            + (humanPlayer_ && callAmount > 0 ? " · You need " + QLocale().toString(callAmount) + " to call" : ""));
        if (!humanPlayer_) actionStatus_->setText("Choose a local player through New Game to view private cards and act.");
        else if (anyAction) actionStatus_->setText("Your turn. Select one of the server-approved actions.");
        else actionStatus_->setText("Waiting for " + view.value("actingSeat").toVariant().toString() + " to act.");
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
            setActionControlsEnabled(false);
            actionStatus_->setText("You are " + humanPlayer_->apiPlayerName() + ". Retrieving your private table view…");
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
            : action == "Raise" ? bluffskill::client::HumanAction::raise
            : bluffskill::client::HumanAction::fold;
        humanPlayer_->selectAction(humanAction);
        if (competition_->currentIndex() < 0 || table_->currentIndex() < 0) return;
        const auto amount = humanAction == bluffskill::client::HumanAction::bet || humanAction == bluffskill::client::HumanAction::raise
            ? amount_->value() : 0;
        setActionControlsEnabled(false);
        amount_->setEnabled(false);
        actionStatus_->setText("Submitting " + bluffskill::client::HumanPlayer::displayName(humanAction) + "…");
        const auto attempt = connectionGeneration_;
        auto* reply = track(humanPlayer_->submitAction(network_, serverUrl_, competition_->currentText(), table_->currentText(), humanAction,
            amount, tableSequence_));
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                const auto detail = response.value("error").toString("The server rejected the action.");
                actionStatus_->setText(detail);
                statusBar()->showMessage("Action was not accepted; refreshing the table.");
                refreshTableView();
                return;
            }
            applyTableView(response);
            statusBar()->showMessage("Action accepted by the server.");
        });
    }

    void setActionControlsEnabled(bool enabled) {
        for (auto* button : actionButtons_) {
            button->setEnabled(enabled);
            const auto actionName = button->property("actionName").toString();
            button->setText(actionName.left(1).toUpper() + actionName.mid(1));
        }
    }

    void clearHumanPlayer() {
        pendingHumanPlayer_.reset();
        humanPlayer_.reset();
        pokerTable_->setLocalPlayerName({});
        setActionControlsEnabled(false);
        amount_->setEnabled(false);
        tableSequence_ = 0;
        actionStatus_->setText("No human player is attached.");
        wagerStatus_->setText("Current bet: 0 chips");
    }

private:
    QNetworkAccessManager network_{this};
    QSet<QNetworkReply*> activeReplies_;
    QNetworkReply* healthReply_{};
    QUrl serverUrl_;
    std::uint64_t connectionGeneration_{};
    std::uint64_t tableRequestGeneration_{};
    std::uint64_t tableViewRequestGeneration_{};
    qint64 tableSequence_{};
    bool connected_{};
    QComboBox* server_{};
    QComboBox* competition_{};
    QComboBox* table_{};
    PokerTable* pokerTable_{};
    QLabel* actionStatus_{};
    QLabel* wagerStatus_{};
    QSpinBox* amount_{};
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
