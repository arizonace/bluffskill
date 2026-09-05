#include "bluffskill/poker/house.hpp"
#include "bluffskill/app_config/app_config.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QCheckBox>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace {

bluffskill::poker::ChipDenominations pokerChipDenominations(const bluffskill::app_config::Settings& settings) {
    bluffskill::poker::ChipDenominations denominations;
    denominations.reserve(static_cast<std::size_t>(settings.chipDenominations.size()));
    for (const auto denomination : settings.chipDenominations) denominations.push_back(denomination);
    return denominations;
}

bluffskill::poker::BlindSchedule pokerBlindSchedule(const bluffskill::app_config::Settings& settings) {
    return {
        .handsPerLevel = static_cast<std::size_t>(settings.blindHandsPerLevel),
        .minutesPerLevel = std::chrono::minutes{settings.blindMinutesPerLevel},
        .smallBlind = static_cast<bluffskill::poker::Chips>(settings.chipDenominations.front()) * settings.smallBlind,
    };
}

QJsonObject tableViewJson(const bluffskill::poker::TableView& table);

class SettingsDialog final : public QDialog {
public:
    explicit SettingsDialog(const bluffskill::app_config::Settings& settings, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("BluffSkill Settings");
        auto* layout = new QFormLayout(this);
        playerClock_ = new QSpinBox(this); playerClock_->setRange(5, 3600); playerClock_->setValue(settings.playerClockSeconds);
        dealClock_ = new QSpinBox(this); dealClock_->setRange(1, 3600); dealClock_->setValue(settings.dealClockSeconds);
        uninterruptedDealerDelay_ = delaySpinBox(settings.uninterruptedDealerDelayMilliseconds, this);
        automatedPlayerDelay_ = delaySpinBox(settings.automatedPlayerDelayMilliseconds, this);
        smallBlind_ = new QSpinBox(this); smallBlind_->setRange(1, 1'000'000); smallBlind_->setValue(settings.smallBlind);
        smallBlind_->setToolTip("Number of smallest chip.");
        blindHands_ = new QSpinBox(this); blindHands_->setRange(1, 10000); blindHands_->setValue(settings.blindHandsPerLevel);
        blindMinutes_ = new QSpinBox(this); blindMinutes_->setRange(1, 3600); blindMinutes_->setValue(settings.blindMinutesPerLevel);
        defaultPlayerName_ = new QLineEdit(settings.defaultPlayerName, this);
        autoConnect_ = new QCheckBox("Automatically try preferred localhost ports", this); autoConnect_->setChecked(settings.clientAutoConnect);
        for (int index = 0; index < 3; ++index) {
            ports_[index] = new QSpinBox(this); ports_[index]->setRange(1, 65535); ports_[index]->setValue(settings.serverPreferredPorts.value(index));
        }
        layout->addRow("Player Clock (seconds)", playerClock_);
        layout->addRow("Deal Clock (seconds)", dealClock_);
        layout->addRow("Uninterrupted Dealer Delay", uninterruptedDealerDelay_);
        layout->addRow("Automated Player Delay", automatedPlayerDelay_);
        layout->addRow("Small Blind (number of smallest chip)", smallBlind_);
        layout->addRow("Blind Increase (hands)", blindHands_);
        layout->addRow("Blind Increase (minutes)", blindMinutes_);
        layout->addRow("Default Player Name", defaultPlayerName_);
        layout->addRow("Preferred Port 1", ports_[0]);
        layout->addRow("Preferred Port 2", ports_[1]);
        layout->addRow("Preferred Port 3", ports_[2]);
        layout->addRow("Client AutoConnect", autoConnect_);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);
    }

    [[nodiscard]] bluffskill::app_config::Settings settings(bluffskill::app_config::Settings value) const {
        value.playerClockSeconds = playerClock_->value();
        value.dealClockSeconds = dealClock_->value();
        value.uninterruptedDealerDelayMilliseconds = milliseconds(*uninterruptedDealerDelay_);
        value.automatedPlayerDelayMilliseconds = milliseconds(*automatedPlayerDelay_);
        value.smallBlind = smallBlind_->value();
        value.blindHandsPerLevel = blindHands_->value();
        value.blindMinutesPerLevel = blindMinutes_->value();
        value.defaultPlayerName = defaultPlayerName_->text();
        value.clientAutoConnect = autoConnect_->isChecked();
        value.serverPreferredPorts.clear();
        for (const auto* port : ports_) value.serverPreferredPorts.append(static_cast<quint16>(port->value()));
        return value;
    }

private:
    static QDoubleSpinBox* delaySpinBox(int milliseconds, QWidget* parent) {
        auto* control = new QDoubleSpinBox(parent);
        control->setRange(0.001, 3'600.0);
        control->setDecimals(3);
        control->setSingleStep(0.100);
        control->setSuffix(" seconds");
        control->setValue(static_cast<double>(milliseconds) / 1'000.0);
        return control;
    }

    static int milliseconds(const QDoubleSpinBox& control) {
        return static_cast<int>(std::llround(control.value() * 1'000.0));
    }

    QSpinBox* playerClock_{};
    QSpinBox* dealClock_{};
    QDoubleSpinBox* uninterruptedDealerDelay_{};
    QDoubleSpinBox* automatedPlayerDelay_{};
    QSpinBox* smallBlind_{};
    QSpinBox* blindHands_{};
    QSpinBox* blindMinutes_{};
    QLineEdit* defaultPlayerName_{};
    QCheckBox* autoConnect_{};
    std::array<QSpinBox*, 3> ports_{};
};

class ServerWindow final : public QMainWindow {
public:
    explicit ServerWindow(bluffskill::app_config::Settings settings)
        : house_(pokerBlindSchedule(settings), pokerChipDenominations(settings)), settings_(std::move(settings)) {
        setWindowTitle("BluffSkill Server");
        resize(1180, 600);
        auto* splitter = new QSplitter(this);
        tree_ = new QTreeWidget(splitter);
        tree_->setHeaderLabels({"House / competition / table / player"});
        log_ = new QPlainTextEdit(splitter);
        log_->setReadOnly(true);
        actionLog_ = new QTableWidget(splitter);
        actionLog_->setColumnCount(7);
        actionLog_->setHorizontalHeaderLabels({"Player", "Kind", "Round", "Action", "Value", "Stack", "Hand"});
        actionLog_->horizontalHeader()->setStretchLastSection(true);
        actionLog_->verticalHeader()->setVisible(false);
        actionLog_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        actionLog_->setSelectionMode(QAbstractItemView::NoSelection);
        actionLog_->setAlternatingRowColors(true);
        tree_->setContextMenuPolicy(Qt::CustomContextMenu);
        splitter->setSizes({300, 380, 500});
        setCentralWidget(splitter);
        auto* fileMenu = menuBar()->addMenu("File");
        auto* saveActionLogAction = fileMenu->addAction("Save Action Log…");
        auto* appMenu = menuBar()->addMenu("Application");
        auto* settingsAction = appMenu->addAction("Settings…");
        auto* aboutAction = appMenu->addAction("About BluffSkill Server");
        connect(saveActionLogAction, &QAction::triggered, this, [this] { saveActionLog(); });
        connect(settingsAction, &QAction::triggered, this, [this] { editSettings(); });
        connect(aboutAction, &QAction::triggered, this, [this] { showAbout(); });
        connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) { showTreeContextMenu(position); });
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
                tableItem->setData(0, Qt::UserRole, QString::fromStdString(competition.name));
                tableItem->setData(0, Qt::UserRole + 1, QString::fromStdString(table.name));
                for (const auto& seatedPlayer : table.players) {
                    const auto playerType = seatedPlayer.player.referenceType
                        ? QString::fromUtf8(bluffskill::poker::toString(*seatedPlayer.player.referenceType)) + " reference player"
                        : QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind));
                    auto* playerItem = new QTreeWidgetItem(tableItem, {QString::number(seatedPlayer.seat) + ": "
                        + QString::fromStdString(seatedPlayer.player.name) + " ("
                        + playerType + ")"});
                    const auto inspection = house_.referencePlayerInspection(competition.name, table.name, seatedPlayer.player.name);
                    if (!inspection) continue;
                    auto* parameters = new QTreeWidgetItem(playerItem, {"Operating parameters"});
                    for (const auto& parameter : inspection->parameters) {
                        new QTreeWidgetItem(parameters, {QString::fromStdString(parameter.name) + ": "
                            + QString::number(parameter.value, 'f', 2)});
                    }
                }
            }
        }
        tree_->expandAll();
        refreshActionLog();
    }

    bluffskill::poker::House& house() noexcept { return house_; }

private:
    void editSettings() {
        SettingsDialog dialog(settings_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        settings_ = dialog.settings(settings_);
        bluffskill::app_config::AppConfig::save(settings_);
        house_.setBlindSchedule(pokerBlindSchedule(settings_));
        QMessageBox::information(this, "Settings saved", "Settings are shared with the client. Blind settings apply to new hands; preferred ports are used the next time the server starts.");
    }

    void showAbout() {
        QMessageBox::about(this, "About BluffSkill Server",
            "BluffSkill Server\nVersion: " + QStringLiteral(BLUFFSKILL_BUILD_VERSION)
                + "\nBuild number: " + QStringLiteral(BLUFFSKILL_BUILD_NUMBER)
                + "\nBuild timestamp: " + QStringLiteral(BLUFFSKILL_BUILD_TIMESTAMP)
                + "\n\n© AzoneLayer · azonelayer.com\nLicensed under the MIT License.");
    }

    struct ActionLogCursor {
        QStringList history;
        std::uint64_t sequence{0};
        QHash<int, qint64> handStartingStacks;
        QSet<int> loggedBustedSeats;
        QSet<int> loggedPotWinnerSeats;
        QSet<int> loggedShowdownHandSeats;
        bool loggedFoldedPot{false};
        bool loggedTableWinner{false};
        qint64 lastBigBlind{0};
    };

    void appendActionLogRow(const QString& player, const QString& kind, const QString& round, const QString& action, const QString& value = {}, const QString& stack = {},
        const QString& hand = {}) {
        const auto row = actionLog_->rowCount();
        actionLog_->insertRow(row);
        actionLog_->setItem(row, 0, new QTableWidgetItem(player));
        actionLog_->setItem(row, 1, new QTableWidgetItem(kind));
        actionLog_->setItem(row, 2, new QTableWidgetItem(round));
        actionLog_->setItem(row, 3, new QTableWidgetItem(action));
        actionLog_->setItem(row, 4, new QTableWidgetItem(value));
        actionLog_->setItem(row, 5, new QTableWidgetItem(stack));
        actionLog_->setItem(row, 6, new QTableWidgetItem(hand));
        actionLog_->item(row, 0)->setData(Qt::UserRole, activeActionLogTableKey_);
        actionLog_->scrollToItem(actionLog_->item(row, 0), QAbstractItemView::PositionAtBottom);
    }

    [[nodiscard]] static QString showdownHand(const bluffskill::poker::TablePlayerView& player) {
        return QString::fromStdString(player.showdownDescription);
    }

    [[nodiscard]] static QString actionLogPlayerKind(const bluffskill::poker::TableSummary& table, std::string_view playerName) {
        const auto player = std::ranges::find_if(table.players, [playerName](const auto& seatedPlayer) {
            return seatedPlayer.player.name == playerName;
        });
        if (player == table.players.end()) return {};
        if (player->player.referenceType) {
            return QString::fromUtf8(bluffskill::poker::toString(*player->player.referenceType));
        }
        return QString::fromUtf8(bluffskill::poker::toString(player->player.kind));
    }

    [[nodiscard]] static QString csvCell(QString value) {
        value.replace('"', "\"\"");
        return '"' + value + '"';
    }

    [[nodiscard]] static QString withSuffix(QString path, const QString& suffix) {
        if (QFileInfo(path).suffix().isEmpty()) path += '.' + suffix;
        return path;
    }

    [[nodiscard]] static QString exportDirectory() {
        QSettings settings;
        const auto savedDirectory = settings.value("server/lastExportDirectory").toString();
        if (!savedDirectory.isEmpty() && QDir(savedDirectory).exists()) return savedDirectory;
        const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        return downloads.isEmpty() ? QDir::homePath() : downloads;
    }

    [[nodiscard]] static QString suggestedExportPath(const QString& fileName) {
        return QDir(exportDirectory()).filePath(fileName);
    }

    static void rememberExportDirectory(const QString& path) {
        QSettings settings;
        settings.setValue("server/lastExportDirectory", QFileInfo(path).absolutePath());
    }

    [[nodiscard]] QJsonArray actionLogJson(const QString& tableKey = {}) const {
        QJsonArray actions;
        int serializedIndex = 1;
        for (int row = 0; row < actionLog_->rowCount(); ++row) {
            const auto* player = actionLog_->item(row, 0);
            if (player == nullptr || (!tableKey.isEmpty() && player->data(Qt::UserRole).toString() != tableKey)) continue;
            actions.append(QJsonObject{{"index", serializedIndex++}, {"player", player->text()},
                {"kind", actionLog_->item(row, 1)->text()},
                {"round", actionLog_->item(row, 2)->text()},
                {"action", actionLog_->item(row, 3)->text()},
                {"value", actionLog_->item(row, 4)->text()},
                {"stack", actionLog_->item(row, 5)->text()},
                {"hand", actionLog_->item(row, 6)->text()}});
        }
        return actions;
    }

    [[nodiscard]] QJsonArray playersJson() const {
        QJsonArray players;
        for (const auto& competition : house_.competitions()) {
            for (const auto& table : competition.tables) {
                const auto tableView = house_.tableView(competition.name, table.name);
                for (const auto& player : tableView.players) {
                    players.append(QJsonObject{{"competition", QString::fromStdString(competition.name)},
                        {"table", QString::fromStdString(table.name)}, {"name", QString::fromStdString(player.name)},
                        {"kind", QString::fromUtf8(bluffskill::poker::toString(player.kind))}, {"seat", static_cast<int>(player.seat)},
                        {"stack", static_cast<qint64>(player.stack)}, {"committed", static_cast<qint64>(player.committed)},
                        {"folded", player.folded}});
                }
            }
        }
        return players;
    }

    bool writeFile(const QString& path, const QByteArray& contents, const QString& description) {
        const auto destination = path;
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, "Save failed", "Could not save " + description + ".\n" + file.errorString());
            return false;
        }
        if (file.write(contents) != contents.size()) {
            QMessageBox::warning(this, "Save failed", "Could not save " + description + ".\n" + file.errorString());
            return false;
        }
        rememberExportDirectory(destination);
        statusBar()->showMessage("Saved " + description + " to " + destination, 5'000);
        return true;
    }

    bool saveActionLogCsv(const QString& path, const QString& tableKey = {}) {
        QString csv = "Index,Player,Kind,Round,Action,Value,Stack,Hand\n";
        int serializedIndex = 1;
        for (int row = 0; row < actionLog_->rowCount(); ++row) {
            const auto* player = actionLog_->item(row, 0);
            if (player == nullptr || (!tableKey.isEmpty() && player->data(Qt::UserRole).toString() != tableKey)) continue;
            csv += QString::number(serializedIndex++);
            for (int column = 0; column < actionLog_->columnCount(); ++column) {
                csv += ',';
                const auto* item = actionLog_->item(row, column);
                csv += csvCell(item == nullptr ? QString{} : item->text());
            }
            csv += '\n';
        }
        return writeFile(withSuffix(path, "csv"), csv.toUtf8(), "Action Log CSV");
    }

    bool saveActionLogJson(const QString& path) {
        const QJsonObject exportData{{"actionLog", actionLogJson()}, {"players", playersJson()}};
        return writeFile(withSuffix(path, "json"), QJsonDocument(exportData).toJson(QJsonDocument::Indented), "Action Log JSON");
    }

    void saveActionLog() {
        QString selectedFilter;
        const auto path = QFileDialog::getSaveFileName(this, "Save Action Log", suggestedExportPath("BluffSkill-action-log"),
            "CSV (*.csv);;JSON (*.json)", &selectedFilter);
        if (path.isEmpty()) return;
        if (selectedFilter.startsWith("JSON")) saveActionLogJson(path);
        else saveActionLogCsv(path);
    }

    void saveTableAsJson(const QString& competitionName, const QString& tableName) {
        const auto path = QFileDialog::getSaveFileName(this, "Save Table as JSON", suggestedExportPath(tableName + ".json"), "JSON (*.json)");
        if (path.isEmpty()) return;
        const auto table = house_.tableView(competitionName.toStdString(), tableName.toStdString());
        writeFile(withSuffix(path, "json"), QJsonDocument(tableViewJson(table)).toJson(QJsonDocument::Indented), "Table JSON");
    }

    void showTreeContextMenu(const QPoint& position) {
        const auto* item = tree_->itemAt(position);
        if (item == nullptr) return;
        const auto competitionName = item->data(0, Qt::UserRole).toString();
        const auto tableName = item->data(0, Qt::UserRole + 1).toString();
        if (competitionName.isEmpty() || tableName.isEmpty()) return;

        QMenu menu(tree_);
        auto* saveTableAction = menu.addAction("Save Table as JSON");
        auto* saveLogAction = menu.addAction("Save Action Log as CSV");
        const auto* selected = menu.exec(tree_->viewport()->mapToGlobal(position));
        if (selected == saveTableAction) saveTableAsJson(competitionName, tableName);
        if (selected == saveLogAction) {
            const auto path = QFileDialog::getSaveFileName(this, "Save Action Log as CSV", suggestedExportPath(tableName + "-action-log.csv"), "CSV (*.csv)");
            if (!path.isEmpty()) saveActionLogCsv(path, competitionName + '/' + tableName);
        }
    }

    static QString actionFingerprint(const bluffskill::poker::ActionView& action) {
        return QString::number(action.seat) + '\x1f' + QString::fromStdString(action.player) + '\x1f'
            + QString::fromUtf8(bluffskill::poker::toString(action.street)) + '\x1f'
            + QString::fromUtf8(bluffskill::poker::toString(action.action)) + '\x1f' + QString::number(action.amount);
    }

    void refreshActionLog() {
        for (const auto& competition : house_.competitions()) {
            for (const auto& table : competition.tables) {
                const auto view = house_.tableView(competition.name, table.name);
                const auto kindFor = [&table](std::string_view playerName) {
                    return actionLogPlayerKind(table, playerName);
                };
                QStringList history;
                for (const auto& action : view.actionHistory) history.append(actionFingerprint(action));
                const auto tableKey = QString::fromStdString(competition.name) + '/' + QString::fromStdString(table.name);
                activeActionLogTableKey_ = tableKey;
                auto& cursor = actionLogCursors_[tableKey];
                const auto continues = cursor.history.size() <= history.size()
                    && std::equal(cursor.history.cbegin(), cursor.history.cend(), history.cbegin());
                const auto appendedActions = continues ? history.size() - cursor.history.size() : 0;
                const auto sequenceIndicatesNewDeal = cursor.sequence != 0
                    && view.eventSequence > cursor.sequence + static_cast<std::uint64_t>(appendedActions);
                const auto newDeal = !history.isEmpty() && (cursor.history.isEmpty() || !continues || sequenceIndicatesNewDeal);
                if (newDeal) {
                    const auto dealer = std::ranges::find_if(view.players, [seat = view.dealerSeat](const auto& player) {
                        return seat && player.seat == *seat;
                    });
                    const auto dealerName = dealer == view.players.end() ? QString{} : QString::fromStdString(dealer->name);
                    const auto dealerKind = dealer == view.players.end() ? QString{} : kindFor(dealer->name);
                    const auto dealerStack = dealer == view.players.end() ? QString{}
                        : QLocale().toString(static_cast<qint64>(dealer->stack + dealer->committed));
                    const auto bigBlind = static_cast<qint64>(view.bigBlind);
                    const auto gameStarted = view.roundsPlayed == 1;
                    if (gameStarted) {
                        appendActionLogRow("Game", {}, "Game", "Start");
                        cursor.loggedBustedSeats.clear();
                        cursor.loggedTableWinner = false;
                    }
                    if (cursor.lastBigBlind > 0 && bigBlind > cursor.lastBigBlind) {
                        appendActionLogRow(dealerName, dealerKind, "Deal", "Blinds Up", QLocale().toString(bigBlind), dealerStack);
                    }
                    appendActionLogRow(dealerName, dealerKind, "Deal", "Deal", {}, dealerStack);
                    cursor.lastBigBlind = bigBlind;
                    cursor.history.clear();
                    cursor.handStartingStacks.clear();
                    cursor.loggedPotWinnerSeats.clear();
                    cursor.loggedShowdownHandSeats.clear();
                    cursor.loggedFoldedPot = false;
                    for (const auto& player : view.players) {
                        if (player.stack > 0 || player.committed > 0) {
                            cursor.handStartingStacks.insert(static_cast<int>(player.seat), static_cast<qint64>(player.stack + player.committed));
                        }
                    }
                }
                for (qsizetype index = cursor.history.size(); index < view.actionHistory.size(); ++index) {
                    const auto& action = view.actionHistory[static_cast<std::size_t>(index)];
                    appendActionLogRow(QString::fromStdString(action.player), kindFor(action.player), QString::fromUtf8(bluffskill::poker::toString(action.street)),
                        QString::fromUtf8(bluffskill::poker::toString(action.action)),
                        action.amount == 0 ? QString{} : QLocale().toString(action.amount), QLocale().toString(action.stackAfter));
                }
                if (view.street == bluffskill::poker::Street::showdown) {
                    QHash<int, qint64> winningsBySeat;
                    for (const auto& payout : view.payouts) {
                        for (const auto& award : payout.awards) {
                            winningsBySeat[static_cast<int>(award.seat)] += static_cast<qint64>(award.amount);
                        }
                    }
                    if (view.showdownOccurred) {
                        for (const auto& player : view.players) {
                            const auto seat = static_cast<int>(player.seat);
                            const auto winnings = winningsBySeat.value(seat);
                            if (winnings > 0 && !cursor.loggedPotWinnerSeats.contains(seat)) {
                                appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", "Pot Won", QLocale().toString(winnings),
                                    QLocale().toString(player.stack), showdownHand(player));
                                cursor.loggedPotWinnerSeats.insert(seat);
                                cursor.loggedShowdownHandSeats.insert(seat);
                            }
                        }
                    } else if (!cursor.loggedFoldedPot) {
                        const auto foldedRound = view.actionHistory.empty()
                            ? QStringLiteral("Showdown")
                            : QString::fromUtf8(bluffskill::poker::toString(view.actionHistory.back().street));
                        for (const auto& player : view.players) {
                            const auto winnings = winningsBySeat.value(static_cast<int>(player.seat));
                            if (winnings <= 0) continue;
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), foldedRound, "Pot Folded", QLocale().toString(winnings),
                                QLocale().toString(player.stack));
                            cursor.loggedFoldedPot = true;
                            break;
                        }
                    }
                    for (const auto& player : view.players) {
                        const auto seat = static_cast<int>(player.seat);
                        if (player.stack == 0 && cursor.handStartingStacks.value(seat) > 0 && !cursor.loggedBustedSeats.contains(seat)) {
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", "Busted Out", {}, QLocale().toString(player.stack),
                                showdownHand(player));
                            cursor.loggedBustedSeats.insert(seat);
                            if (view.showdownOccurred) cursor.loggedShowdownHandSeats.insert(seat);
                        }
                    }
                    if (view.showdownOccurred) {
                        for (const auto& player : view.players) {
                            const auto seat = static_cast<int>(player.seat);
                            if (player.folded || cursor.loggedShowdownHandSeats.contains(seat)) continue;
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", "Hand", {}, QLocale().toString(player.stack),
                                showdownHand(player));
                            cursor.loggedShowdownHandSeats.insert(seat);
                        }
                    }
                    const auto tableWinner = std::ranges::find_if(view.players, [](const auto& player) { return player.stack > 0; });
                    const auto remainingPlayers = std::count_if(view.players.begin(), view.players.end(), [](const auto& player) { return player.stack > 0; });
                    if (remainingPlayers == 1 && !cursor.loggedTableWinner) {
                        appendActionLogRow(QString::fromStdString(tableWinner->name), kindFor(tableWinner->name), "Game", "Table Winner", QLocale().toString(tableWinner->stack),
                            QLocale().toString(tableWinner->stack));
                        cursor.loggedTableWinner = true;
                    }
                }
                cursor.history = std::move(history);
                cursor.sequence = view.eventSequence;
            }
        }
    }

    bluffskill::poker::House house_;
    bluffskill::app_config::Settings settings_;
    QTreeWidget* tree_{};
    QPlainTextEdit* log_{};
    QTableWidget* actionLog_{};
    QHash<QString, ActionLogCursor> actionLogCursors_;
    QString activeActionLogTableKey_;
};

QJsonObject competitionJson(const bluffskill::poker::CompetitionSummary& competition) {
    QJsonArray players;
    for (const auto& table : competition.tables) {
        for (const auto& seatedPlayer : table.players) {
            QJsonObject player{{"name", QString::fromStdString(seatedPlayer.player.name)},
                {"kind", QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind))},
                {"table", QString::fromStdString(table.name)}, {"seat", static_cast<int>(seatedPlayer.seat)}};
            if (seatedPlayer.player.referenceType) {
                player.insert("referenceType", QString::fromUtf8(bluffskill::poker::toString(*seatedPlayer.player.referenceType)));
            }
            players.append(player);
        }
    }
    QJsonArray chipDenominations;
    for (const auto denomination : competition.chipDenominations) chipDenominations.append(static_cast<qint64>(denomination));
    return {{"name", QString::fromStdString(competition.name)},
            {"style", "Tournament"},
            {"table", QString::fromStdString(competition.tables.front().name)},
            {"maximumPlayers", static_cast<int>(competition.tournament.maximumPlayers)},
            {"startingStack", static_cast<int>(competition.tournament.startingStack)},
            {"chipDenominations", chipDenominations},
            {"referencePlayers", players}};
}

QJsonObject tableJson(const bluffskill::poker::TableSummary& table) {
    QJsonArray players;
    for (const auto& seatedPlayer : table.players) {
        QJsonObject player{{"name", QString::fromStdString(seatedPlayer.player.name)},
            {"kind", QString::fromUtf8(bluffskill::poker::toString(seatedPlayer.player.kind))},
            {"seat", static_cast<int>(seatedPlayer.seat)}};
        if (seatedPlayer.player.referenceType) {
            player.insert("referenceType", QString::fromUtf8(bluffskill::poker::toString(*seatedPlayer.player.referenceType)));
        }
        players.append(player);
    }
    return {{"name", QString::fromStdString(table.name)},
            {"maximumSeats", static_cast<int>(table.maximumSeats)},
            {"players", players}};
}

QJsonObject tableViewJson(const bluffskill::poker::TableView& table) {
    QJsonArray chipDenominations;
    for (const auto denomination : table.chipDenominations) chipDenominations.append(static_cast<qint64>(denomination));
    QJsonArray players;
    for (const auto& player : table.players) {
        QJsonArray holeCards;
        for (const auto card : player.holeCards) holeCards.append(QString::fromStdString(bluffskill::cards::toString(card)));
        players.append(QJsonObject{{"name", QString::fromStdString(player.name)},
            {"kind", QString::fromUtf8(bluffskill::poker::toString(player.kind))},
            {"seat", static_cast<int>(player.seat)}, {"stack", static_cast<qint64>(player.stack)},
            {"committed", static_cast<qint64>(player.committed)}, {"roundCommitted", static_cast<qint64>(player.roundCommitted)},
            {"folded", player.folded},
            {"dealer", player.dealer}, {"acting", player.acting}, {"holeCards", holeCards},
            {"showdownDescription", QString::fromStdString(player.showdownDescription)}});
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
        {"smallBlind", static_cast<qint64>(table.smallBlind)}, {"bigBlind", static_cast<qint64>(table.bigBlind)},
        {"chipDenominations", chipDenominations},
        {"blindLevel", static_cast<qint64>(table.blindLevel)}, {"roundsPlayed", static_cast<qint64>(table.roundsPlayed)},
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

std::optional<bluffskill::poker::ReferencePlayerType> parseReferencePlayerType(const QString& value) {
    if (value.isEmpty() || value.compare("Leo", Qt::CaseInsensitive) == 0) return bluffskill::poker::ReferencePlayerType::leo;
    if (value.compare("Virgo", Qt::CaseInsensitive) == 0) return bluffskill::poker::ReferencePlayerType::virgo;
    return std::nullopt;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("AzoneLayer");
    QCoreApplication::setOrganizationDomain("azonelayer.com");
    const auto settings = bluffskill::app_config::AppConfig::load();
    ServerWindow window(settings);
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
                    .maximumPlayers = static_cast<std::size_t>(json.value("maximumPlayers").toInt(10)),
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

    server.route("/v1/competitions/<arg>/tables/<arg>/restart", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            try {
                const auto viewerName = QJsonDocument::fromJson(request.body()).object().value("viewer").toString();
                window.house().restartTable(competitionName.toStdString(), tableName.toStdString());
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/restart → 200");
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/restart → 409");
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
            const auto type = parseReferencePlayerType(json.value("type").toString());
            if (!type) {
                window.log("POST /v1/competitions/" + competitionName + "/reference-players → 400");
                return QHttpServerResponse(QJsonObject{{"error", "type must be Leo or Virgo"}}, QHttpServerResponder::StatusCode::BadRequest);
            }
            try {
                const auto competition = window.house().createReferencePlayers(
                    competitionName.toStdString(), static_cast<std::size_t>(json.value("count").toInt()), *type);
                window.refreshTree();
                window.log("POST /v1/competitions/" + competitionName + "/reference-players → 201");
                return QHttpServerResponse(competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                window.log("POST /v1/competitions/" + competitionName + "/reference-players → 400");
                return QHttpServerResponse(QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    QTcpServer tcpServer;
    bool listening = false;
    for (const auto port : settings.serverPreferredPorts) {
        if (tcpServer.listen(QHostAddress::LocalHost, port)) {
            listening = true;
            break;
        }
    }
    if (!listening && !tcpServer.listen(QHostAddress::LocalHost, 0)) return 1;
    if (!server.bind(&tcpServer)) return 1;
    const auto port = tcpServer.serverPort();
    window.log("Listening on http://127.0.0.1:" + QString::number(port));
    window.log("Prototype REST endpoints: GET/POST /v1/competitions");
    window.show();
    return application.exec();
}
