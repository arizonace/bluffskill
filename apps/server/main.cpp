#include "bluffskill/poker/house.hpp"
#include "bluffskill/app_config/app_config.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
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
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

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
        .smallBlind = settings.smallBlind,
    };
}

QJsonObject tableViewJson(const bluffskill::poker::TableView& table);

class SettingsDialog final : public QDialog {
public:
    explicit SettingsDialog(const bluffskill::app_config::Settings& settings, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("BluffSkill Settings");
        auto* root = new QVBoxLayout(this);
        auto* tabs = new QTabWidget(this);
        auto* general = new QWidget(tabs);
        auto* layout = new QFormLayout(general);
        smallBlind_ = new QLineEdit(QString::number(settings.smallBlind), this);
        stack_ = new QLineEdit(QString::number(settings.stack), this);
        blindHands_ = new QSpinBox(this); blindHands_->setRange(1, 10000); blindHands_->setValue(settings.blindHandsPerLevel);
        blindMinutes_ = new QSpinBox(this); blindMinutes_->setRange(1, 3600); blindMinutes_->setValue(settings.blindMinutesPerLevel);
        defaultPlayerName_ = new QLineEdit(settings.defaultPlayerName, this);
        detailedServerLogs_ = new QCheckBox("Include JSON request and response bodies in the local server log", this);
        detailedServerLogs_->setChecked(settings.detailedServerLogs);
        storeActionsInJson_ = new QCheckBox("Store Actions in JSON", this);
        storeActionsInJson_->setChecked(settings.storeActionsInJson);
        for (int index = 0; index < 3; ++index) {
            ports_[index] = new QSpinBox(this); ports_[index]->setRange(1, 65535); ports_[index]->setValue(settings.serverPreferredPorts.value(index));
        }
        layout->addRow("Small Blind", smallBlind_);
        layout->addRow("Stack", stack_);
        layout->addRow("Blind Increase (hands)", blindHands_);
        layout->addRow("Blind Increase (minutes)", blindMinutes_);
        layout->addRow("Default Player Name", defaultPlayerName_);
        layout->addRow("Preferred Port 1", ports_[0]);
        layout->addRow("Preferred Port 2", ports_[1]);
        layout->addRow("Preferred Port 3", ports_[2]);
        layout->addRow("Detailed Server Logs", detailedServerLogs_);
        layout->addRow(storeActionsInJson_);
        tabs->addTab(general, "General");

        auto* columns = new QWidget(tabs);
        auto* columnsLayout = new QFormLayout(columns);
        for (const auto* column : optionalActionLogColumns_) {
            auto* visible = new QCheckBox("Visible", columns);
            const auto name = QString::fromUtf8(column);
            visible->setChecked(settings.serverActionLogVisibleColumns.contains(name));
            actionLogColumns_.insert(name, visible);
            columnsLayout->addRow(name, visible);
        }
        tabs->addTab(columns, "Action Log Columns");
        root->addWidget(tabs);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this, settings] {
            bool smallBlindValid = false;
            bool stackValid = false;
            const auto smallBlind = smallBlind_->text().toLongLong(&smallBlindValid);
            const auto stack = stack_->text().toLongLong(&stackValid);
            const auto smallestChip = settings.chipDenominations.front();
            if (!smallBlindValid || !stackValid || smallBlind <= 0 || stack <= 0
                || smallBlind % smallestChip != 0 || stack % smallestChip != 0) {
                QMessageBox::warning(this, "Invalid chip amounts",
                    "Small Blind and Stack must be positive integer multiples of the smallest chip denomination ("
                        + QLocale().toString(smallestChip) + ").");
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    [[nodiscard]] bluffskill::app_config::Settings settings(bluffskill::app_config::Settings value) const {
        value.smallBlind = smallBlind_->text().toLongLong();
        value.stack = stack_->text().toLongLong();
        value.blindHandsPerLevel = blindHands_->value();
        value.blindMinutesPerLevel = blindMinutes_->value();
        value.defaultPlayerName = defaultPlayerName_->text();
        value.detailedServerLogs = detailedServerLogs_->isChecked();
        value.storeActionsInJson = storeActionsInJson_->isChecked();
        value.serverActionLogVisibleColumns.clear();
        for (const auto* column : optionalActionLogColumns_) {
            const auto name = QString::fromUtf8(column);
            if (actionLogColumns_.value(name)->isChecked()) value.serverActionLogVisibleColumns.append(name);
        }
        value.serverPreferredPorts.clear();
        for (const auto* port : ports_) value.serverPreferredPorts.append(static_cast<quint16>(port->value()));
        return value;
    }

private:
    static constexpr std::array<const char*, 9> optionalActionLogColumns_{
        "Timestamp", "Game", "Round", "Street", "Kind", "Stack", "Gain", "Pot", "Details"};
    QLineEdit* smallBlind_{};
    QLineEdit* stack_{};
    QSpinBox* blindHands_{};
    QSpinBox* blindMinutes_{};
    QLineEdit* defaultPlayerName_{};
    QCheckBox* detailedServerLogs_{};
    QCheckBox* storeActionsInJson_{};
    QHash<QString, QCheckBox*> actionLogColumns_;
    std::array<QSpinBox*, 3> ports_{};
};

class ServerWindow final : public QMainWindow {
public:
    enum ActionLogColumn {
        indexColumn,
        timestampColumn,
        gameColumn,
        roundColumn,
        streetColumn,
        playerColumn,
        kindColumn,
        actionColumn,
        valueColumn,
        stackColumn,
        gainColumn,
        potColumn,
        detailsColumn,
        holeColumn,
        actionLogColumnCount,
    };

    explicit ServerWindow(bluffskill::app_config::Settings settings)
        : house_(pokerBlindSchedule(settings), pokerChipDenominations(settings)), settings_(std::move(settings)) {
        initializeServerLog();
        initializeRecoveryBackingStores();
        connect(&serverLogRotationTimer_, &QTimer::timeout, this, [this] {
            rotateServerLogIfNeeded();
            scheduleServerLogRotation();
        });
        setWindowTitle("BluffSkill Server");
        resize(1800, 680);
        auto* splitter = new QSplitter(this);
        tree_ = new QTreeWidget(splitter);
        tree_->setHeaderLabels({"House / competition / table / player"});
        actionLog_ = new QTableWidget(splitter);
        actionLog_->setColumnCount(actionLogColumnCount);
        actionLog_->setHorizontalHeaderLabels(
            {"Index", "Timestamp", "Game", "Round", "Street", "Player", "Kind", "Action", "Value", "Stack", "Gain", "Pot", "Details", "Hole"});
        actionLog_->setMinimumWidth(1400);
        actionLog_->setColumnWidth(indexColumn, 70);
        actionLog_->setColumnWidth(timestampColumn, 150);
        actionLog_->setColumnWidth(gameColumn, 220);
        actionLog_->setColumnWidth(roundColumn, 70);
        actionLog_->setColumnWidth(streetColumn, 90);
        actionLog_->setColumnWidth(playerColumn, 150);
        actionLog_->setColumnWidth(kindColumn, 90);
        actionLog_->setColumnWidth(actionColumn, 110);
        actionLog_->setColumnWidth(valueColumn, 100);
        actionLog_->setColumnWidth(stackColumn, 100);
        actionLog_->setColumnWidth(gainColumn, 100);
        actionLog_->setColumnWidth(potColumn, 100);
        applyActionLogColumnVisibility();
        actionLog_->horizontalHeader()->setStretchLastSection(true);
        actionLog_->verticalHeader()->setVisible(false);
        actionLog_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        actionLog_->setSelectionMode(QAbstractItemView::NoSelection);
        actionLog_->setAlternatingRowColors(true);
        tree_->setContextMenuPolicy(Qt::CustomContextMenu);
        splitter->setSizes({360, 1440});
        setCentralWidget(splitter);
        auto* fileMenu = menuBar()->addMenu("File");
        auto* saveActionLogAction = fileMenu->addAction("Save Action Log…");
        auto* serverMenu = menuBar()->addMenu("Server");
        clearHouseAction_ = serverMenu->addAction("Clear House");
        auto* clearLogAction = serverMenu->addAction("Clear Log");
        auto* appMenu = menuBar()->addMenu("Application");
        auto* settingsAction = appMenu->addAction("Settings…");
        auto* aboutAction = appMenu->addAction("About BluffSkill Server");
        connect(saveActionLogAction, &QAction::triggered, this, [this] { saveActionLog(); });
        connect(clearHouseAction_, &QAction::triggered, this, [this] { clearHouse(); });
        connect(clearLogAction, &QAction::triggered, this, [this] { clearLog(); });
        connect(settingsAction, &QAction::triggered, this, [this] { editSettings(); });
        connect(aboutAction, &QAction::triggered, this, [this] { showAbout(); });
        connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) { showTreeContextMenu(position); });
        refreshTree();
    }

    ~ServerWindow() override {
        QFile::remove(runningMarkerPath());
    }

    void log(const QString& message) {
        rotateServerLogIfNeeded();
        QFile file(serverLogPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
        file.write((QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss  ") + message + '\n').toUtf8());
    }

    void logRequestJson(const QHttpServerRequest& request) {
        if (!settings_.detailedServerLogs || request.body().isEmpty()) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(request.body(), &error);
        const auto json = error.error == QJsonParseError::NoError
            ? QString::fromUtf8(document.toJson(QJsonDocument::Compact)) : QString::fromUtf8(request.body());
        log("  Request JSON: " + json);
    }

    void logResponseJson(const QString& summary, const QJsonObject& body) {
        log(summary);
        if (settings_.detailedServerLogs) log("  Response JSON: " + QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
    }

    void logResponseJson(const QString& summary, const QJsonArray& body) {
        log(summary);
        if (settings_.detailedServerLogs) log("  Response JSON: " + QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
    }

    [[nodiscard]] QHttpServerResponse jsonResponse(const QString& summary, const QJsonObject& body,
        QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::Ok) {
        logResponseJson(summary, body);
        return QHttpServerResponse(body, status);
    }

    [[nodiscard]] QHttpServerResponse jsonResponse(const QString& summary, const QJsonArray& body,
        QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::Ok) {
        logResponseJson(summary, body);
        return QHttpServerResponse(body, status);
    }

    void refreshTree(bool updatePlayerBackingStore = false) {
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
        if (updatePlayerBackingStore) writePlayersBackingStore();
        refreshClearHouseAction();
    }

    bluffskill::poker::House& house() noexcept { return house_; }

    [[nodiscard]] bluffskill::poker::CompetitionSummary createCompetition(std::size_t maximumPlayers) {
        auto competitionSettings = settings_;
        const auto smallestChip = competitionSettings.chipDenominations.front();
        const auto amountsAreValid = competitionSettings.smallBlind > 0 && competitionSettings.stack > 0
            && competitionSettings.smallBlind % smallestChip == 0 && competitionSettings.stack % smallestChip == 0;
        if (!amountsAreValid) {
            competitionSettings.smallBlind = smallestChip;
            competitionSettings.stack = smallestChip * 300;
            log("Error: [chips] smallBlind and stack must be positive multiples of the smallest denomination; "
                "using smallBlind=" + QString::number(competitionSettings.smallBlind)
                + " and stack=" + QString::number(competitionSettings.stack) + " for this competition.");
        }
        house_.setDefaultCompetitionSettings(pokerBlindSchedule(competitionSettings), pokerChipDenominations(competitionSettings));
        return house_.createSingleTableTournament({
            .maximumPlayers = maximumPlayers,
            .startingStack = competitionSettings.stack,
        });
    }

    [[nodiscard]] bluffskill::poker::TableWinner quitGame(const QString& competitionName, const QString& tableName, const QString& playerName) {
        const auto competition = house_.competition(competitionName.toStdString());
        if (!competition) throw std::invalid_argument("competition was not found");
        const auto table = std::ranges::find_if(competition->tables, [&tableName](const auto& candidate) {
            return QString::fromStdString(candidate.name) == tableName;
        });
        if (table == competition->tables.end()) throw std::invalid_argument("table was not found");
        if (!playerName.isEmpty()) {
            const auto player = std::ranges::find_if(table->players, [&playerName](const auto& candidate) {
                return QString::fromStdString(candidate.player.name) == playerName;
            });
            if (player == table->players.end() || player->player.kind != bluffskill::poker::PlayerKind::api) {
                throw std::invalid_argument("player is not an API player at this table");
            }
        }

        // Bring the ordinary hand log through the final accepted action before
        // the quit transition changes the table sequence without adding one.
        refreshActionLog();
        const auto winner = house_.quitGame(competitionName.toStdString(), tableName.toStdString());
        const auto afterQuit = house_.tableView(competitionName.toStdString(), tableName.toStdString());
        const auto tableKey = competitionName + '/' + tableName;
        actionLogCursors_[tableKey].sequence = afterQuit.eventSequence;
        refreshTree();
        activeActionLogTableKey_ = tableKey;
        activeActionLogCompetitionName_ = competitionName;
        activeActionLogTableName_ = tableName;
        activeActionLogGameName_ = QString::fromStdString(afterQuit.gameName);
        const auto round = QString::number(afterQuit.roundsPlayed);
        const auto kindFor = [table](std::string_view name) { return actionLogPlayerKind(*table, name); };
        appendActionLogRow(playerName.isEmpty() ? "Dealer" : playerName,
            playerName.isEmpty() ? QString{} : kindFor(playerName.toStdString()), "Game", round, "Quit Game");
        appendActionLogRow(QString::fromStdString(winner.player), kindFor(winner.player), "Game", round, "Table Winner",
            QLocale().toString(winner.chips), QLocale().toString(winner.chips));
        actionLogCursors_[activeActionLogTableKey_].loggedTableWinner = true;
        return winner;
    }

private:
    [[nodiscard]] static QString serverLogDirectory() {
        return QDir::home().filePath("AzoneLayer/BluffSkill");
    }

    [[nodiscard]] static QString serverLogPath() {
        return QDir(serverLogDirectory()).filePath("server.log");
    }

    [[nodiscard]] static QString actionLogPath() {
        return QDir(serverLogDirectory()).filePath("actions.csv");
    }

    [[nodiscard]] static QString playersCsvPath() {
        return QDir(serverLogDirectory()).filePath("players.csv");
    }

    [[nodiscard]] static QString runningMarkerPath() {
        return QDir(serverLogDirectory()).filePath("bluffskill-server.dirty");
    }

    [[nodiscard]] static QString recoveredPath(const QString& sourcePath) {
        const QFileInfo source(sourcePath);
        const auto timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        auto destination = source.dir().filePath(source.completeBaseName() + '-' + timestamp + "-recovered." + source.suffix());
        for (int suffix = 1; QFile::exists(destination); ++suffix) {
            destination = source.dir().filePath(source.completeBaseName() + '-' + timestamp + "-recovered-" + QString::number(suffix)
                + '.' + source.suffix());
        }
        return destination;
    }

    void preserveBackingStoreAfterCrash(const QString& sourcePath) {
        if (!QFile::exists(sourcePath)) return;
        const auto destination = recoveredPath(sourcePath);
        if (QFile::rename(sourcePath, destination)) {
            log("Recovered unclean-run backing store as " + destination);
        } else {
            log("Error: could not preserve unclean-run backing store " + sourcePath);
        }
    }

    void initializeRecoveryBackingStores() {
        QDir().mkpath(serverLogDirectory());
        if (QFile::exists(runningMarkerPath())) {
            log("Detected an unclean previous server exit; preserving its CSV backing stores.");
            preserveBackingStoreAfterCrash(actionLogPath());
            preserveBackingStoreAfterCrash(playersCsvPath());
        }

        QFile marker(runningMarkerPath());
        const auto markerContents = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
            || marker.write(markerContents) != markerContents.size() || !marker.flush()) {
            log("Error: could not create server running marker: " + marker.errorString());
        }
        initializeActionLogBackingStore();
        initializePlayersBackingStore();
    }

    void initializeActionLogBackingStore() {
        QFile file(actionLogPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            log("Error: could not initialize persistent action log: " + file.errorString());
            return;
        }
        const auto header = actionLogCsvHeader().toUtf8();
        if (file.write(header) != header.size() || !file.flush()) {
            log("Error: could not write persistent action log: " + file.errorString());
        }
    }

    void initializePlayersBackingStore() {
        QFile file(playersCsvPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            log("Error: could not initialize persistent player log: " + file.errorString());
            return;
        }
        const auto header = playersCsvHeader().toUtf8();
        if (file.write(header) != header.size() || !file.flush()) {
            log("Error: could not write persistent player log: " + file.errorString());
        }
    }

    void initializeServerLog() {
        QDir().mkpath(serverLogDirectory());
        const QFileInfo current(serverLogPath());
        serverLogDate_ = current.exists() ? current.lastModified().date() : QDate::currentDate();
        rotateServerLogIfNeeded();
        removeExpiredServerLogs();
        scheduleServerLogRotation();
    }

    void rotateServerLogIfNeeded() {
        const auto today = QDate::currentDate();
        if (!serverLogDate_.isValid()) serverLogDate_ = today;
        if (serverLogDate_ == today) return;
        const auto currentPath = serverLogPath();
        if (QFile::exists(currentPath)) {
            auto datedPath = QDir(serverLogDirectory()).filePath("server-" + serverLogDate_.toString("yyyyMMdd") + ".log");
            for (int suffix = 1; QFile::exists(datedPath); ++suffix) {
                datedPath = QDir(serverLogDirectory()).filePath("server-" + serverLogDate_.toString("yyyyMMdd")
                    + '-' + QString::number(suffix) + ".log");
            }
            QFile::rename(currentPath, datedPath);
        }
        serverLogDate_ = today;
        removeExpiredServerLogs();
    }

    void removeExpiredServerLogs() const {
        const auto earliestKept = QDate::currentDate().addDays(-6);
        const QDir directory(serverLogDirectory());
        for (const auto& file : directory.entryInfoList({"server-*.log"}, QDir::Files)) {
            if (file.lastModified().date() < earliestKept) QFile::remove(file.absoluteFilePath());
        }
    }

    void scheduleServerLogRotation() {
        const auto now = QDateTime::currentDateTime();
        const auto nextMidnight = QDateTime(now.date().addDays(1), QTime(0, 0));
        serverLogRotationTimer_.start(static_cast<int>(std::max<qint64>(1, now.msecsTo(nextMidnight) + 50)));
    }

    void editSettings() {
        SettingsDialog dialog(settings_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        settings_ = dialog.settings(settings_);
        bluffskill::app_config::AppConfig::save(settings_);
        house_.setDefaultCompetitionSettings(pokerBlindSchedule(settings_), pokerChipDenominations(settings_));
        applyActionLogColumnVisibility();
        QMessageBox::information(this, "Settings saved",
            "Server and client settings have separate controls in their shared file. Small Blind and Stack apply when the next competition starts; existing competitions are unchanged.");
    }

    void showAbout() {
        QMessageBox::about(this, "About BluffSkill Server",
            "BluffSkill Server\nVersion: " + QStringLiteral(BLUFFSKILL_BUILD_VERSION)
                + "\nBuild number: " + QStringLiteral(BLUFFSKILL_BUILD_NUMBER)
                + "\nBuild timestamp: " + QStringLiteral(BLUFFSKILL_BUILD_TIMESTAMP)
                + "\n\n© AzoneLayer · azonelayer.com\nLicensed under the MIT License.");
    }

    void clearLog() {
        actionLog_->setRowCount(0);
        actionLogCursors_.clear();
        activeActionLogTableKey_.clear();
        initializeActionLogBackingStore();
        log("Server action log cleared");
    }

    void clearHouse() {
        if (house_.hasGamesInProgress()) {
            refreshClearHouseAction();
            return;
        }
        house_.clear();
        clearLog();
        initializePlayersBackingStore();
        log("Server house cleared");
        refreshTree();
    }

    void refreshClearHouseAction() {
        if (clearHouseAction_ != nullptr) clearHouseAction_->setEnabled(!house_.hasGamesInProgress());
    }

    void applyActionLogColumnVisibility() {
        const auto visible = [this](const char* name) {
            return settings_.serverActionLogVisibleColumns.contains(QString::fromUtf8(name));
        };
        actionLog_->setColumnHidden(indexColumn, true);
        actionLog_->setColumnHidden(timestampColumn, !visible("Timestamp"));
        actionLog_->setColumnHidden(gameColumn, !visible("Game"));
        actionLog_->setColumnHidden(roundColumn, !visible("Round"));
        actionLog_->setColumnHidden(streetColumn, !visible("Street"));
        actionLog_->setColumnHidden(playerColumn, false);
        actionLog_->setColumnHidden(kindColumn, !visible("Kind"));
        actionLog_->setColumnHidden(actionColumn, false);
        actionLog_->setColumnHidden(valueColumn, false);
        actionLog_->setColumnHidden(stackColumn, !visible("Stack"));
        actionLog_->setColumnHidden(gainColumn, !visible("Gain"));
        actionLog_->setColumnHidden(potColumn, !visible("Pot"));
        actionLog_->setColumnHidden(detailsColumn, !visible("Details"));
        actionLog_->setColumnHidden(holeColumn, true);
    }

    struct ActionLogCursor {
        QStringList history;
        std::uint64_t sequence{0};
        QHash<int, qint64> handStartingStacks;
        QSet<int> loggedBustedSeats;
        QSet<int> loggedPotWinnerSeats;
        QSet<int> loggedShowdownHandSeats;
        bool loggedFoldedPot{false};
        bool loggedPayouts{false};
        bool loggedTableWinner{false};
        std::size_t loggedCommunityCards{0};
        qint64 lastBigBlind{0};
    };

    void appendActionLogRow(const QString& player, const QString& kind, const QString& street, const QString& round, const QString& action,
        const QString& value = {}, const QString& stack = {}, const QString& gain = {}, const QString& pot = {}, const QString& details = {},
        const QString& hole = {}) {
        const auto row = actionLog_->rowCount();
        actionLog_->insertRow(row);
        const auto qualifiedGame = activeActionLogCompetitionName_ + ':' + activeActionLogGameName_;
        actionLog_->setItem(row, indexColumn, new QTableWidgetItem(QString::number(row + 1)));
        actionLog_->setItem(row, timestampColumn, new QTableWidgetItem(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
        actionLog_->setItem(row, gameColumn, new QTableWidgetItem(qualifiedGame));
        actionLog_->setItem(row, roundColumn, new QTableWidgetItem(round));
        actionLog_->setItem(row, streetColumn, new QTableWidgetItem(street));
        actionLog_->setItem(row, playerColumn, new QTableWidgetItem(player));
        actionLog_->setItem(row, kindColumn, new QTableWidgetItem(kind));
        actionLog_->setItem(row, actionColumn, new QTableWidgetItem(action));
        actionLog_->setItem(row, valueColumn, new QTableWidgetItem(value));
        actionLog_->setItem(row, stackColumn, new QTableWidgetItem(stack));
        actionLog_->setItem(row, gainColumn, new QTableWidgetItem(gain));
        actionLog_->setItem(row, potColumn, new QTableWidgetItem(pot));
        actionLog_->setItem(row, detailsColumn, new QTableWidgetItem(details));
        actionLog_->setItem(row, holeColumn, new QTableWidgetItem(hole));
        actionLog_->item(row, playerColumn)->setData(Qt::UserRole, activeActionLogTableKey_);
        actionLog_->item(row, playerColumn)->setData(Qt::UserRole + 1, activeActionLogCompetitionName_);
        actionLog_->item(row, playerColumn)->setData(Qt::UserRole + 2, activeActionLogTableName_);
        actionLog_->item(row, playerColumn)->setData(Qt::UserRole + 3, activeActionLogGameName_);
        actionLog_->scrollToItem(actionLog_->item(row, playerColumn), QAbstractItemView::PositionAtBottom);
        if (!appendActionLogBackingStoreRow(row)) log("Error: could not append action row to persistent action log.");
    }

    [[nodiscard]] static QString pokerNotation(bluffskill::cards::Card card) {
        const auto rank = card.rank == bluffskill::cards::Rank::ten
            ? QStringLiteral("10")
            : QString::fromStdString(bluffskill::cards::toString(card.rank));
        switch (card.suit) {
        case bluffskill::cards::Suit::spades: return rank + 's';
        case bluffskill::cards::Suit::hearts: return rank + 'h';
        case bluffskill::cards::Suit::diamonds: return rank + 'd';
        case bluffskill::cards::Suit::clubs: return rank + 'c';
        }
        return {};
    }

    [[nodiscard]] static QString pokerNotation(std::vector<bluffskill::cards::Card> cards) {
        std::stable_sort(cards.begin(), cards.end(), [](const auto& left, const auto& right) { return left.rank > right.rank; });
        QStringList notation;
        for (const auto card : cards) notation.append(pokerNotation(card));
        return notation.join(' ');
    }

    [[nodiscard]] static QString showdownDetails(const bluffskill::poker::TablePlayerView& player) {
        const auto description = QString::fromStdString(player.showdownDescription);
        if (player.holeCards.empty()) return description;
        const auto cards = pokerNotation(player.holeCards);
        return description.isEmpty() ? cards : description + " - " + cards;
    }

    [[nodiscard]] static QString payoutRecipients(const bluffskill::poker::TableView& view,
        const bluffskill::poker::PayoutView& payout) {
        QStringList recipients;
        for (const auto& award : payout.awards) {
            const auto player = std::ranges::find_if(view.players, [seat = award.seat](const auto& candidate) {
                return candidate.seat == seat;
            });
            const auto name = player == view.players.end() ? QStringLiteral("Seat %1").arg(award.seat)
                                                         : QString::fromStdString(player->name);
            recipients.append(name + " (" + QLocale().toString(static_cast<qint64>(award.amount)) + ')');
        }
        return recipients.join(", ");
    }

    [[nodiscard]] QString holeCardsForLog(std::string_view competitionName, std::string_view tableName,
        std::string_view playerName) const {
        if (!house_.recordsHoleCardsWhenFolding(competitionName, tableName, playerName)) return {};
        const auto playerView = house_.tableView(competitionName, tableName, playerName);
        const auto player = std::ranges::find_if(playerView.players, [playerName](const auto& candidate) {
            return candidate.name == playerName;
        });
        return player == playerView.players.end() ? QString{} : pokerNotation(player->holeCards);
    }

    [[nodiscard]] static QString communityCards(const bluffskill::poker::TableView& view, std::size_t first, std::size_t count) {
        std::vector<bluffskill::cards::Card> cards;
        const auto last = std::min(first + count, view.communityCards.size());
        cards.insert(cards.end(), view.communityCards.begin() + static_cast<std::ptrdiff_t>(first),
            view.communityCards.begin() + static_cast<std::ptrdiff_t>(last));
        return pokerNotation(std::move(cards));
    }

    void appendCommunityCardLogRows(const bluffskill::poker::TableView& view, ActionLogCursor& cursor, std::size_t visibleCards) {
        if (cursor.loggedCommunityCards == 0 && visibleCards >= 3 && view.communityCards.size() >= 3) {
            appendActionLogRow("Dealer", {}, "Flop", QString::number(view.roundsPlayed), "Reveal", {}, {}, {}, {}, communityCards(view, 0, 3));
            cursor.loggedCommunityCards = 3;
        }
        if (cursor.loggedCommunityCards == 3 && visibleCards >= 4 && view.communityCards.size() >= 4) {
            appendActionLogRow("Dealer", {}, "Turn", QString::number(view.roundsPlayed), "Reveal", {}, {}, {}, {}, communityCards(view, 3, 1));
            cursor.loggedCommunityCards = 4;
        }
        if (cursor.loggedCommunityCards == 4 && visibleCards >= 5 && view.communityCards.size() >= 5) {
            appendActionLogRow("Dealer", {}, "River", QString::number(view.roundsPlayed), "Reveal", {}, {}, {}, {}, communityCards(view, 4, 1));
            cursor.loggedCommunityCards = 5;
        }
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

    [[nodiscard]] static QString actionLogCsvHeader() {
        return "Index,Timestamp,Game,Round,Street,Player,Kind,Action,Value,Stack,Gain,Pot,Details,Hole\n";
    }

    [[nodiscard]] static QString playersCsvHeader(const QStringList& profileColumns = {}) {
        QStringList columns{"Competition", "Table", "Name", "Kind", "Type", "Seat", "Stack", "Committed", "Folded"};
        columns.append(profileColumns);
        return columns.join(',') + '\n';
    }

    [[nodiscard]] QString actionLogCsvRow(int row, int serializedIndex) const {
        QString csv = QString::number(serializedIndex);
        for (int column = timestampColumn; column < actionLog_->columnCount(); ++column) {
            csv += ',';
            const auto* item = actionLog_->item(row, column);
            csv += csvCell(item == nullptr ? QString{} : item->text());
        }
        return csv + '\n';
    }

    bool appendActionLogBackingStoreRow(int row) const {
        QFile file(actionLogPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return false;
        const auto csv = actionLogCsvRow(row, row + 1).toUtf8();
        return file.write(csv) == csv.size() && file.flush();
    }

    bool copyBackingStore(const QString& sourcePath, const QString& destination, const QString& description) {
        QFile backingStore(sourcePath);
        if (!backingStore.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) || !backingStore.flush()) {
            QMessageBox::warning(this, "Save failed", "Could not flush " + description + ".\n" + backingStore.errorString());
            return false;
        }
        backingStore.close();
        if (QFile::exists(destination) && !QFile::remove(destination)) {
            QMessageBox::warning(this, "Save failed", "Could not replace " + description + ".\n" + destination);
            return false;
        }
        if (!QFile::copy(sourcePath, destination)) {
            QMessageBox::warning(this, "Save failed", "Could not copy " + description + ".\n" + destination);
            return false;
        }
        return true;
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
        return QDir(exportDirectory()).filePath(QDateTime::currentDateTime().toString("HHmm-") + fileName);
    }

    [[nodiscard]] static QString actionLogExportDirectory() {
        QSettings settings;
        const auto savedDirectory = settings.value("server/lastActionLogExportDirectory").toString();
        if (!savedDirectory.isEmpty() && QDir(savedDirectory).exists()) return savedDirectory;

        const auto legacyDirectory = settings.value("server/lastExportDirectory").toString();
        const auto legacyInfo = QFileInfo(legacyDirectory);
        if (legacyInfo.exists() && QRegularExpression("^\\d{4}-BluffSkill-action-log$").match(legacyInfo.fileName()).hasMatch()) {
            return legacyInfo.dir().absolutePath();
        }
        return exportDirectory();
    }

    [[nodiscard]] static QString suggestedActionLogExportPath() {
        return QDir(actionLogExportDirectory()).filePath(QDateTime::currentDateTime().toString("HHmm-") + "BluffSkill-action-log");
    }

    static void rememberExportDirectory(const QString& path) {
        QSettings settings;
        settings.setValue("server/lastExportDirectory", QFileInfo(path).absolutePath());
    }

    static void rememberActionLogExportDirectory(const QString& folderPath) {
        QSettings settings;
        settings.setValue("server/lastActionLogExportDirectory", QFileInfo(folderPath).absolutePath());
    }

    [[nodiscard]] QJsonObject actionLogRecord(int row, int index) const {
        return {{"index", index}, {"timestamp", actionLog_->item(row, timestampColumn)->text()},
            {"player", actionLog_->item(row, playerColumn)->text()}, {"kind", actionLog_->item(row, kindColumn)->text()},
            {"street", actionLog_->item(row, streetColumn)->text()}, {"round", actionLog_->item(row, roundColumn)->text()},
            {"action", actionLog_->item(row, actionColumn)->text()}, {"value", actionLog_->item(row, valueColumn)->text()},
            {"stack", actionLog_->item(row, stackColumn)->text()}, {"gain", actionLog_->item(row, gainColumn)->text()},
            {"pot", actionLog_->item(row, potColumn)->text()}, {"details", actionLog_->item(row, detailsColumn)->text()},
            {"holeCards", actionLog_->item(row, holeColumn)->text()}};
    }

    [[nodiscard]] static QString gameConstitution(const bluffskill::poker::CompetitionSummary& competition, const QString& tableName) {
        const auto table = std::ranges::find_if(competition.tables, [&tableName](const auto& candidate) {
            return QString::fromStdString(candidate.name) == tableName;
        });
        if (table == competition.tables.end()) return {};
        QHash<QString, int> counts;
        QStringList order;
        for (const auto& seated : table->players) {
            const auto type = seated.player.referenceType
                ? QString::fromUtf8(bluffskill::poker::toString(*seated.player.referenceType))
                : QString::fromUtf8(bluffskill::poker::toString(seated.player.kind));
            if (!counts.contains(type)) order.append(type);
            ++counts[type];
        }
        QStringList entries;
        for (const auto& type : order) entries.append(QString::number(counts.value(type)) + ' ' + type);
        return entries.join(", ");
    }

    [[nodiscard]] static QString gameDuration(const QDateTime& firstDeal, const QDateTime& winner) {
        if (!firstDeal.isValid() || !winner.isValid()) return {};
        const auto seconds = std::max<qint64>(0, firstDeal.secsTo(winner));
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600, 2, 10, QChar('0'))
            .arg((seconds / 60) % 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0'));
    }

    [[nodiscard]] QJsonObject gameLogJson(const bluffskill::poker::CompetitionSummary& competition, const QString& tableName,
        const QString& gameName, const QJsonArray& actions) const {
        int rounds = 0;
        QDateTime firstDeal;
        QDateTime winnerTimestamp;
        QString winner;
        QString winnerKind;
        QString runnerUp;
        QString runnerUpKind;
        for (const auto& value : actions) {
            const auto action = value.toObject();
            rounds = std::max(rounds, action.value("round").toString().toInt());
            const auto actionName = action.value("action").toString();
            if (actionName == "Deal" && !firstDeal.isValid()) {
                firstDeal = QDateTime::fromString(action.value("timestamp").toString(), "yyyy-MM-dd HH:mm:ss");
            }
            if (actionName == "Busted Out") {
                runnerUp = action.value("player").toString();
                runnerUpKind = action.value("kind").toString();
            }
            if (actionName == "Table Winner") {
                winner = action.value("player").toString();
                winnerKind = action.value("kind").toString();
                winnerTimestamp = QDateTime::fromString(action.value("timestamp").toString(), "yyyy-MM-dd HH:mm:ss");
            }
        }
        QJsonObject result{{"name", gameName}, {"table", tableName}, {"rounds", rounds}, {"duration", gameDuration(firstDeal, winnerTimestamp)},
            {"winner", winner}, {"winnerKind", winnerKind}, {"runnerUp", runnerUp}, {"runnerUpKind", runnerUpKind},
            {"constitution", gameConstitution(competition, tableName)}};
        if (settings_.storeActionsInJson) result.insert("actions", actions);
        return result;
    }

    [[nodiscard]] QJsonArray actionLogJson() const {
        QJsonArray competitions;
        for (const auto& competition : house_.competitions()) {
            const auto competitionName = QString::fromStdString(competition.name);
            QJsonArray games;
            QSet<QString> seenGames;
            for (int row = 0; row < actionLog_->rowCount(); ++row) {
                const auto* player = actionLog_->item(row, playerColumn);
                if (player == nullptr || player->data(Qt::UserRole + 1).toString() != competitionName) continue;
                const auto gameName = player->data(Qt::UserRole + 3).toString();
                const auto tableName = player->data(Qt::UserRole + 2).toString();
                const auto gameKey = tableName + '\x1f' + gameName;
                if (seenGames.contains(gameKey)) continue;
                seenGames.insert(gameKey);
                QJsonArray actions;
                int index = 1;
                for (int gameRow = 0; gameRow < actionLog_->rowCount(); ++gameRow) {
                    const auto* gamePlayer = actionLog_->item(gameRow, playerColumn);
                    if (gamePlayer == nullptr || gamePlayer->data(Qt::UserRole + 1).toString() != competitionName
                        || gamePlayer->data(Qt::UserRole + 2).toString() != tableName
                        || gamePlayer->data(Qt::UserRole + 3).toString() != gameName) continue;
                    actions.append(actionLogRecord(gameRow, index++));
                }
                games.append(gameLogJson(competition, tableName, gameName, actions));
            }
            if (!games.isEmpty()) competitions.append(QJsonObject{{"competition_name", competitionName}, {"games", games}});
        }
        return competitions;
    }

    [[nodiscard]] QJsonArray playersJson() const {
        QJsonArray competitions;
        for (const auto& competition : house_.competitions()) {
            QJsonArray players;
            for (const auto& table : competition.tables) {
                const auto tableView = house_.tableView(competition.name, table.name);
                for (const auto& player : tableView.players) {
                    QJsonObject profile;
                    auto type = QString::fromUtf8(bluffskill::poker::toString(player.kind));
                    if (const auto inspection = house_.referencePlayerInspection(competition.name, table.name, player.name)) {
                        type = QString::fromUtf8(bluffskill::poker::toString(inspection->type));
                        for (const auto& parameter : inspection->parameters) {
                            profile.insert(QString::fromStdString(parameter.name), parameter.value);
                        }
                    }
                    players.append(QJsonObject{{"table", QString::fromStdString(table.name)}, {"name", QString::fromStdString(player.name)},
                        {"kind", QString::fromUtf8(bluffskill::poker::toString(player.kind))}, {"seat", static_cast<int>(player.seat)},
                        {"stack", static_cast<qint64>(player.stack)}, {"committed", static_cast<qint64>(player.committed)},
                        {"folded", player.folded}, {"type", type}, {"profile", profile}});
                }
            }
            competitions.append(QJsonObject{{"competition_name", QString::fromStdString(competition.name)}, {"table_players", players}});
        }
        return competitions;
    }

    void writePlayersBackingStore() {
        struct PlayerRecord {
            QString competition;
            QJsonObject player;
        };

        QList<PlayerRecord> players;
        QSet<QString> profileColumns;
        for (const auto& competitionValue : playersJson()) {
            const auto competition = competitionValue.toObject();
            const auto competitionName = competition.value("competition_name").toString();
            for (const auto& playerValue : competition.value("table_players").toArray()) {
                const auto player = playerValue.toObject();
                const auto profile = player.value("profile").toObject();
                for (auto it = profile.begin(); it != profile.end(); ++it) profileColumns.insert(it.key());
                players.append({competitionName, player});
            }
        }

        auto sortedProfileColumns = profileColumns.values();
        std::sort(sortedProfileColumns.begin(), sortedProfileColumns.end(), [](const auto& left, const auto& right) {
            return QString::compare(left, right, Qt::CaseInsensitive) < 0;
        });
        QString csv = playersCsvHeader(sortedProfileColumns);
        for (const auto& record : players) {
            const auto profile = record.player.value("profile").toObject();
            QStringList values{record.competition, record.player.value("table").toString(), record.player.value("name").toString(),
                record.player.value("kind").toString(), record.player.value("type").toString(),
                QString::number(record.player.value("seat").toInt()), QString::number(record.player.value("stack").toInteger()),
                QString::number(record.player.value("committed").toInteger()), record.player.value("folded").toBool() ? "true" : "false"};
            for (const auto& column : sortedProfileColumns) {
                const auto value = profile.value(column);
                values.append(value.isDouble() ? QString::number(value.toDouble(), 'g', 16) : value.toString());
            }
            for (auto& value : values) value = csvCell(value);
            csv += values.join(',') + '\n';
        }

        QFile file(playersCsvPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            log("Error: could not update persistent player log: " + file.errorString());
            return;
        }
        const auto contents = csv.toUtf8();
        if (file.write(contents) != contents.size() || !file.flush()) {
            log("Error: could not write persistent player log: " + file.errorString());
        }
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
        QString csv = actionLogCsvHeader();
        int serializedIndex = 1;
        for (int row = 0; row < actionLog_->rowCount(); ++row) {
            const auto* player = actionLog_->item(row, playerColumn);
            if (player == nullptr || (!tableKey.isEmpty() && player->data(Qt::UserRole).toString() != tableKey)) continue;
            csv += actionLogCsvRow(row, serializedIndex++);
        }
        return writeFile(withSuffix(path, "csv"), csv.toUtf8(), "Action Log CSV");
    }

    bool saveActionLogJson(const QString& path) {
        const QJsonObject exportData{{"competitions", actionLogJson()}, {"players", playersJson()}};
        return writeFile(withSuffix(path, "json"), QJsonDocument(exportData).toJson(QJsonDocument::Indented), "Action Log JSON");
    }

    void saveActionLog() {
        const auto folderPath = QFileDialog::getSaveFileName(this, "Save Action Log Folder",
            suggestedActionLogExportPath(), "Action Log Folder (*)");
        if (folderPath.isEmpty()) return;
        if (!QDir().mkpath(folderPath)) {
            QMessageBox::warning(this, "Save failed", "Could not create the action-log folder.\n" + folderPath);
            return;
        }
        const QDir folder(folderPath);
        const auto savedCsv = copyBackingStore(actionLogPath(), folder.filePath("actions.csv"), "Action Log CSV");
        const auto savedPlayers = copyBackingStore(playersCsvPath(), folder.filePath("players.csv"), "Player CSV");
        const auto savedJson = saveActionLogJson(folder.filePath("summary.json"));
        rememberActionLogExportDirectory(folderPath);
        if (savedCsv && savedPlayers && savedJson) statusBar()->showMessage("Saved action log to " + folderPath, 5'000);
    }

    void saveTableAsJson(const QString& competitionName, const QString& tableName) {
        const auto path = QFileDialog::getSaveFileName(this, "Save Table as JSON", suggestedExportPath(tableName + ".json"), "JSON (*.json)");
        if (path.isEmpty()) return;
        const auto table = house_.tableView(competitionName.toStdString(), tableName.toStdString());
        auto exportData = tableViewJson(table);
        auto players = exportData.value("players").toArray();
        for (auto playerValue : players) {
            auto player = playerValue.toObject();
            QJsonObject profile;
            auto type = player.value("kind").toString();
            if (const auto inspection = house_.referencePlayerInspection(competitionName.toStdString(), tableName.toStdString(),
                    player.value("name").toString().toStdString())) {
                type = QString::fromUtf8(bluffskill::poker::toString(inspection->type));
                for (const auto& parameter : inspection->parameters) {
                    profile.insert(QString::fromStdString(parameter.name), parameter.value);
                }
            }
            player.insert("type", type);
            player.insert("profile", profile);
            playerValue = player;
        }
        exportData.insert("players", players);
        writeFile(withSuffix(path, "json"), QJsonDocument(exportData).toJson(QJsonDocument::Indented), "Table JSON");
    }

    void showTreeContextMenu(const QPoint& position) {
        const auto* item = tree_->itemAt(position);
        if (item == nullptr) return;
        const auto competitionName = item->data(0, Qt::UserRole).toString();
        const auto tableName = item->data(0, Qt::UserRole + 1).toString();
        if (competitionName.isEmpty() || tableName.isEmpty()) return;

        QMenu menu(tree_);
        auto* saveTableAction = menu.addAction("Save Table as JSON");
        const auto* selected = menu.exec(tree_->viewport()->mapToGlobal(position));
        if (selected == saveTableAction) saveTableAsJson(competitionName, tableName);
    }

    static QString actionFingerprint(const bluffskill::poker::ActionView& action) {
        return QString::number(action.seat) + '\x1f' + QString::fromStdString(action.player) + '\x1f'
            + QString::fromUtf8(bluffskill::poker::toString(action.street)) + '\x1f'
            + QString::fromUtf8(bluffskill::poker::toString(action.action)) + '\x1f' + QString::number(action.amount);
    }

    [[nodiscard]] static QString actionAnnotations(const bluffskill::poker::TableView& view, std::size_t actionIndex) {
        const auto& action = view.actionHistory.at(actionIndex);
        QStringList annotations;

        bool fullBigBlindWasPosted = false;
        bool preflopWasRaised = false;
        bluffskill::poker::Chips roundCommitted = 0;
        for (std::size_t index = 0; index <= actionIndex; ++index) {
            const auto& earlier = view.actionHistory.at(index);
            if (earlier.street == action.street && earlier.seat == action.seat) roundCommitted += earlier.amount;
            if (index == actionIndex || earlier.street != bluffskill::poker::Street::preflop) continue;
            if (earlier.action == bluffskill::poker::Action::bigBlind) fullBigBlindWasPosted = earlier.amount == view.bigBlind;
            if (earlier.action == bluffskill::poker::Action::bet || earlier.action == bluffskill::poker::Action::raise) preflopWasRaised = true;
        }

        const auto completesBigBlind = action.street == bluffskill::poker::Street::preflop
            && action.action == bluffskill::poker::Action::call
            && view.smallBlindSeat && action.seat == *view.smallBlindSeat
            && fullBigBlindWasPosted && !preflopWasRaised;
        if (completesBigBlind) annotations.append("Complete");
        if (action.stackAfter == 0 && roundCommitted < view.bigBlind) annotations.append("Less");
        if (action.stackAfter == 0) annotations.append("All In");
        return annotations.join(", ");
    }

    [[nodiscard]] static bluffskill::poker::Chips committedThroughAction(const bluffskill::poker::TableView& view,
        std::size_t seat, std::size_t lastActionIndex) {
        bluffskill::poker::Chips committed = 0;
        const auto actionCount = std::min(lastActionIndex + 1, view.actionHistory.size());
        for (std::size_t index = 0; index < actionCount; ++index) {
            const auto& action = view.actionHistory.at(index);
            if (action.seat == seat) committed += action.amount;
        }
        return committed;
    }

    [[nodiscard]] static QString lostValue(bluffskill::poker::Chips amount) {
        return amount > 0 ? '-' + QLocale().toString(amount) : QString{};
    }

    [[nodiscard]] static QString gainedValue(bluffskill::poker::Chips amount) {
        return amount > 0 ? '+' + QLocale().toString(amount) : QString{};
    }

    [[nodiscard]] static QString netValue(bluffskill::poker::Chips amount) {
        return amount >= 0 ? gainedValue(amount) : lostValue(-amount);
    }

    [[nodiscard]] static qint64 totalPot(const bluffskill::poker::TableView& view) {
        qint64 total = 0;
        for (const auto& pot : view.pots) total += static_cast<qint64>(pot.amount);
        return total;
    }

    void refreshActionLog() {
        for (const auto& competition : house_.competitions()) {
            for (const auto& table : competition.tables) {
                const auto view = house_.tableView(competition.name, table.name);
                const auto kindFor = [&table](std::string_view playerName) {
                    return actionLogPlayerKind(table, playerName);
                };
                const auto round = QString::number(view.roundsPlayed);
                QStringList history;
                for (const auto& action : view.actionHistory) history.append(actionFingerprint(action));
                const auto tableKey = QString::fromStdString(competition.name) + '/' + QString::fromStdString(table.name);
                activeActionLogTableKey_ = tableKey;
                activeActionLogCompetitionName_ = QString::fromStdString(competition.name);
                activeActionLogTableName_ = QString::fromStdString(table.name);
                activeActionLogGameName_ = QString::fromStdString(view.gameName);
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
                        appendActionLogRow("Dealer", {}, "Game", round, "Start");
                        cursor.loggedBustedSeats.clear();
                        cursor.loggedTableWinner = false;
                    }
                    if (cursor.lastBigBlind > 0 && bigBlind > cursor.lastBigBlind) {
                        appendActionLogRow(dealerName, dealerKind, "Deal", round, "Blinds Up", QLocale().toString(bigBlind), dealerStack);
                    }
                    const auto remainingPlayers = std::count_if(view.players.begin(), view.players.end(), [](const auto& player) {
                        return player.stack > 0 || player.committed > 0;
                    });
                    appendActionLogRow(dealerName, dealerKind, "Deal", round, "Deal", QString::number(remainingPlayers), dealerStack,
                        {}, {}, remainingPlayers == 2 ? QStringLiteral("Heads Up") : QString{});
                    cursor.lastBigBlind = bigBlind;
                    cursor.history.clear();
                    cursor.handStartingStacks.clear();
                    cursor.loggedPotWinnerSeats.clear();
                    cursor.loggedShowdownHandSeats.clear();
                    cursor.loggedFoldedPot = false;
                    cursor.loggedPayouts = false;
                    cursor.loggedCommunityCards = 0;
                    for (const auto& player : view.players) {
                        if (player.stack > 0 || player.committed > 0) {
                            cursor.handStartingStacks.insert(static_cast<int>(player.seat), static_cast<qint64>(player.stack + player.committed));
                        }
                    }
                }
                for (qsizetype index = cursor.history.size(); index < view.actionHistory.size(); ++index) {
                    const auto& action = view.actionHistory[static_cast<std::size_t>(index)];
                    const auto visibleCards = action.street == bluffskill::poker::Street::flop ? std::size_t{3}
                        : action.street == bluffskill::poker::Street::turn ? std::size_t{4}
                        : action.street == bluffskill::poker::Street::river ? std::size_t{5} : std::size_t{0};
                    appendCommunityCardLogRows(view, cursor, visibleCards);
                    const auto folded = action.action == bluffskill::poker::Action::fold;
                    const auto value = folded || action.amount == 0 ? QString{} : QLocale().toString(action.amount);
                    const auto gain = folded
                        ? lostValue(committedThroughAction(view, action.seat, static_cast<std::size_t>(index))) : QString{};
                    const auto holeCards = folded
                        ? holeCardsForLog(competition.name, table.name, action.player) : QString{};
                    appendActionLogRow(QString::fromStdString(action.player), kindFor(action.player), QString::fromUtf8(bluffskill::poker::toString(action.street)),
                        round, QString::fromUtf8(bluffskill::poker::toString(action.action)), value, QLocale().toString(action.stackAfter), gain,
                        QLocale().toString(action.potAfter), actionAnnotations(view, static_cast<std::size_t>(index)), holeCards);
                }
                appendCommunityCardLogRows(view, cursor, view.communityCards.size());
                if (view.street == bluffskill::poker::Street::showdown) {
                    const auto potValue = totalPot(view);
                    QHash<int, qint64> winningsBySeat;
                    for (const auto& payout : view.payouts) {
                        for (const auto& award : payout.awards) {
                            winningsBySeat[static_cast<int>(award.seat)] += static_cast<qint64>(award.amount);
                        }
                    }
                    if (view.payouts.size() > 1 && !cursor.loggedPayouts) {
                        const auto payoutStreet = view.showdownOccurred ? QStringLiteral("Showdown")
                            : view.actionHistory.empty() ? QStringLiteral("Showdown")
                            : QString::fromUtf8(bluffskill::poker::toString(view.actionHistory.back().street));
                        for (std::size_t index = 0; index < view.payouts.size(); ++index) {
                            const auto& payout = view.payouts[index];
                            const auto action = index == 0 ? QStringLiteral("Main Pot")
                                : QStringLiteral("Side Pot %1").arg(index);
                            appendActionLogRow("Dealer", {}, payoutStreet, round, action, {}, {}, {},
                                QLocale().toString(static_cast<qint64>(payout.amount)), payoutRecipients(view, payout));
                        }
                        cursor.loggedPayouts = true;
                    }
                    if (view.showdownOccurred) {
                        for (const auto& player : view.players) {
                            const auto seat = static_cast<int>(player.seat);
                            const auto winnings = winningsBySeat.value(seat);
                            if (winnings > 0 && !cursor.loggedPotWinnerSeats.contains(seat)) {
                                const auto netGain = winnings - static_cast<qint64>(player.committed);
                                appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", round, "Pot Won", QLocale().toString(potValue),
                                    QLocale().toString(player.stack), netValue(netGain), QLocale().toString(potValue), showdownDetails(player), pokerNotation(player.holeCards));
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
                            const auto netGain = winnings - static_cast<qint64>(player.committed);
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), foldedRound, round, "Pot Folded", QLocale().toString(potValue),
                                QLocale().toString(player.stack), netValue(netGain), QLocale().toString(potValue), {},
                                holeCardsForLog(competition.name, table.name, player.name));
                            cursor.loggedFoldedPot = true;
                            break;
                        }
                    }
                    for (const auto& player : view.players) {
                        const auto seat = static_cast<int>(player.seat);
                        if (player.stack == 0 && cursor.handStartingStacks.value(seat) > 0 && !cursor.loggedBustedSeats.contains(seat)) {
                            const auto lost = committedThroughAction(view, player.seat, view.actionHistory.size()) - winningsBySeat.value(seat);
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", round, "Busted Out", {}, QLocale().toString(player.stack),
                                lostValue(lost), QLocale().toString(potValue), showdownDetails(player), pokerNotation(player.holeCards));
                            cursor.loggedBustedSeats.insert(seat);
                            if (view.showdownOccurred) cursor.loggedShowdownHandSeats.insert(seat);
                        }
                    }
                    if (view.showdownOccurred) {
                        for (const auto& player : view.players) {
                            const auto seat = static_cast<int>(player.seat);
                            if (player.folded || cursor.loggedShowdownHandSeats.contains(seat)) continue;
                            const auto lost = committedThroughAction(view, player.seat, view.actionHistory.size()) - winningsBySeat.value(seat);
                            appendActionLogRow(QString::fromStdString(player.name), kindFor(player.name), "Showdown", round, "Lost", {}, QLocale().toString(player.stack),
                                lostValue(lost), QLocale().toString(potValue), showdownDetails(player), pokerNotation(player.holeCards));
                            cursor.loggedShowdownHandSeats.insert(seat);
                        }
                    }
                    const auto tableWinner = std::ranges::find_if(view.players, [](const auto& player) { return player.stack > 0; });
                    const auto remainingPlayers = std::count_if(view.players.begin(), view.players.end(), [](const auto& player) { return player.stack > 0; });
                    if (remainingPlayers == 1 && !cursor.loggedTableWinner) {
                        appendActionLogRow(QString::fromStdString(tableWinner->name), kindFor(tableWinner->name), "Game", round, "Table Winner", QLocale().toString(tableWinner->stack),
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
    QTableWidget* actionLog_{};
    QAction* clearHouseAction_{};
    QTimer serverLogRotationTimer_{this};
    QDate serverLogDate_;
    QHash<QString, ActionLogCursor> actionLogCursors_;
    QString activeActionLogTableKey_;
    QString activeActionLogCompetitionName_;
    QString activeActionLogTableName_;
    QString activeActionLogGameName_;
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
            {"game", QString::fromStdString(competition.tables.front().gameName)},
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
            {"game", QString::fromStdString(table.gameName)},
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
            {"action", QString::fromUtf8(bluffskill::poker::toString(action.action))}, {"amount", static_cast<qint64>(action.amount)},
            {"stackAfter", static_cast<qint64>(action.stackAfter)}, {"pot", static_cast<qint64>(action.potAfter)},
            {"currentBet", static_cast<qint64>(action.currentBetAfter)}});
    }
    QJsonObject legalActions;
    if (table.legalActions) {
        const auto& legal = *table.legalActions;
        legalActions = {{"check", legal.check}, {"call", legal.call}, {"bet", legal.bet}, {"raise", legal.raise}, {"fold", legal.fold},
            {"callAmount", static_cast<qint64>(legal.callAmount)}, {"minimumAmount", static_cast<qint64>(legal.minimumAmount)},
            {"maximumAmount", static_cast<qint64>(legal.maximumAmount)}};
    }
    return {{"name", QString::fromStdString(table.name)}, {"game", QString::fromStdString(table.gameName)},
        {"sequence", static_cast<qint64>(table.eventSequence)},
        {"street", QString::fromUtf8(bluffskill::poker::toString(table.street))},
        {"startingStack", static_cast<qint64>(table.startingStack)}, {"currentBet", static_cast<qint64>(table.currentBet)},
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

QJsonArray referencePlayerTypesJson(const bluffskill::poker::House& house) {
    QJsonArray types;
    for (const auto type : house.referencePlayerTypes()) types.append(QString::fromUtf8(bluffskill::poker::toString(type)));
    return types;
}

std::optional<bluffskill::poker::ReferencePlayerType> parseReferencePlayerType(const QString& value,
    const bluffskill::poker::House& house) {
    if (value.isEmpty()) return bluffskill::poker::ReferencePlayerType::leo;
    const auto types = house.referencePlayerTypes();
    const auto type = std::ranges::find_if(types, [&value](const auto candidate) {
        return value.compare(QString::fromUtf8(bluffskill::poker::toString(candidate)), Qt::CaseInsensitive) == 0;
    });
    if (type != types.end()) return *type;
    return std::nullopt;
}

QString referencePlayerTypeNames(const bluffskill::poker::House& house) {
    QStringList names;
    for (const auto type : house.referencePlayerTypes()) names.append(QString::fromUtf8(bluffskill::poker::toString(type)));
    return names.join(", ");
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
        return window.jsonResponse("GET /v1/health → 200",
            QJsonObject{{"status", "ok"}, {"referencePlayerTypes", referencePlayerTypesJson(window.house())}});
    });

    server.route("/v1/competitions", QHttpServerRequest::Method::Post,
        [&window](const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            const auto json = QJsonDocument::fromJson(request.body()).object();
            try {
                const auto competition = window.createCompetition(static_cast<std::size_t>(json.value("maximumPlayers").toInt(10)));
                window.refreshTree();
                return window.jsonResponse("POST /v1/competitions → 201 (" + QString::fromStdString(competition.name) + ')',
                    competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions → 400", QJsonObject{{"error", exception.what()}},
                    QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    server.route("/v1/competitions", [&window] {
        QJsonArray competitions;
        for (const auto& competition : window.house().competitions()) competitions.append(competitionJson(competition));
        return window.jsonResponse("GET /v1/competitions → 200", competitions);
    });

    server.route("/v1/competitions/<arg>/tables", [&window](const QString& competitionName) -> QHttpServerResponse {
        const auto competition = window.house().competition(competitionName.toStdString());
        if (!competition) {
            return window.jsonResponse("GET /v1/competitions/" + competitionName + "/tables → 404",
                QJsonObject{{"error", "competition was not found"}}, QHttpServerResponder::StatusCode::NotFound);
        }
        QJsonArray tables;
        for (const auto& table : competition->tables) tables.append(tableJson(table));
        return window.jsonResponse("GET /v1/competitions/" + competitionName + "/tables → 200", tables);
    });

    server.route("/v1/competitions/<arg>/tables/<arg>/view", [&window](const QString& competitionName, const QString& tableName,
        const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto viewerName = QUrlQuery(request.url()).queryItemValue("viewer");
            try {
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.logResponseJson("GET /v1/competitions/" + competitionName + "/tables/" + tableName + "/view → 200", tableViewJson(table));
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                return window.jsonResponse("GET /v1/competitions/" + competitionName + "/tables/" + tableName + "/view → 404",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::NotFound);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/actions", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
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
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 400",
                    QJsonObject{{"error", "action, player, expectedSequence, and a non-negative integer amount are required"}},
                    QHttpServerResponder::StatusCode::BadRequest);
            }
            const auto amount = static_cast<bluffskill::poker::Chips>(amountValue);
            const auto sequence = static_cast<std::uint64_t>(sequenceValue);
            try {
                window.house().submitAction(competitionName.toStdString(), tableName.toStdString(), playerName.toStdString(), *action, amount, sequence);
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), playerName.toStdString());
                window.refreshTree();
                window.logResponseJson("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 200", tableViewJson(table));
                return tableViewResponse(table);
            } catch (const bluffskill::poker::CommandError& exception) {
                const auto status = exception.failure() == bluffskill::poker::CommandFailure::illegalAction
                    ? QHttpServerResponder::StatusCode::UnprocessableEntity : QHttpServerResponder::StatusCode::Conflict;
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → "
                    + (status == QHttpServerResponder::StatusCode::Conflict ? "409" : "422"), QJsonObject{{"error", exception.what()}}, status);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/actions → 400",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/next-hand", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            try {
                const auto viewerName = QJsonDocument::fromJson(request.body()).object().value("viewer").toString();
                window.house().startNextHand(competitionName.toStdString(), tableName.toStdString());
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.refreshTree();
                window.logResponseJson("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/next-hand → 200", tableViewJson(table));
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName
                    + "/next-hand → 409: " + QString::fromUtf8(exception.what()), QJsonObject{{"error", exception.what()}},
                    QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/restart", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            try {
                const auto viewerName = QJsonDocument::fromJson(request.body()).object().value("viewer").toString();
                window.house().restartTable(competitionName.toStdString(), tableName.toStdString());
                const auto table = window.house().tableView(competitionName.toStdString(), tableName.toStdString(), viewerName.toStdString());
                window.refreshTree();
                window.logResponseJson("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/restart → 200", tableViewJson(table));
                return tableViewResponse(table);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/restart → 409",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/quit", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            try {
                const auto playerName = QJsonDocument::fromJson(request.body()).object().value("player").toString();
                const auto winner = window.quitGame(competitionName, tableName, playerName);
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/quit → 200",
                    QJsonObject{{"winner", QString::fromStdString(winner.player)}, {"seat", static_cast<int>(winner.seat)},
                        {"chips", static_cast<qint64>(winner.chips)}});
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/quit → 409",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/pause", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest&) -> QHttpServerResponse {
            try {
                window.house().pauseTable(competitionName.toStdString(), tableName.toStdString());
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/pause → 200",
                    QJsonObject{{"status", "paused"}});
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/pause → 409",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/resume", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest&) -> QHttpServerResponse {
            try {
                window.house().resumeTable(competitionName.toStdString(), tableName.toStdString());
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/resume → 200",
                    QJsonObject{{"status", "running"}});
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/resume → 409",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::Conflict);
            }
        });

    server.route("/v1/competitions/<arg>/tables/<arg>/players", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QString& tableName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            const auto json = QJsonDocument::fromJson(request.body()).object();
            try {
                const auto competition = window.house().createApiPlayer(
                    competitionName.toStdString(), tableName.toStdString(), json.value("name").toString().toStdString(),
                    json.value("recordHoleCards").toBool(false));
                window.refreshTree(true);
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/players → 201",
                    competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/tables/" + tableName + "/players → 400",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
            }
        });

    server.route("/v1/competitions/<arg>/reference-players", QHttpServerRequest::Method::Post,
        [&window](const QString& competitionName, const QHttpServerRequest& request) -> QHttpServerResponse {
            window.logRequestJson(request);
            const auto json = QJsonDocument::fromJson(request.body()).object();
            const auto type = parseReferencePlayerType(json.value("type").toString(), window.house());
            if (!type) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/reference-players → 400",
                    QJsonObject{{"error", "type must be one of: " + referencePlayerTypeNames(window.house())}},
                    QHttpServerResponder::StatusCode::BadRequest);
            }
            try {
                const auto competition = window.house().createReferencePlayers(
                    competitionName.toStdString(), static_cast<std::size_t>(json.value("count").toInt()), *type);
                window.refreshTree(true);
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/reference-players → 201",
                    competitionJson(competition), QHttpServerResponder::StatusCode::Created);
            } catch (const std::exception& exception) {
                return window.jsonResponse("POST /v1/competitions/" + competitionName + "/reference-players → 400",
                    QJsonObject{{"error", exception.what()}}, QHttpServerResponder::StatusCode::BadRequest);
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
