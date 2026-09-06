#include "human_player.hpp"
#include "bluffskill/app_config/app_config.hpp"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QTimer>
#include <QLineEdit>
#include <QLocale>
#include <QVector>

#include <memory>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {

constexpr int defaultTableSeats = 10;

struct CompactCard {
    QString rank;
    QString suit;
    QColor color;
};

[[nodiscard]] CompactCard compactCard(const QString& description) {
    const auto pieces = description.split(' ', Qt::SkipEmptyParts);
    const auto rank = pieces.value(0, "?");
    const auto suitName = pieces.value(2).toLower();
    if (suitName == "clubs") return {rank, "♣", QColor("#172018")};
    if (suitName == "diamonds") return {rank, "♦", QColor("#C82B30")};
    if (suitName == "hearts") return {rank, "♥", QColor("#C82B30")};
    if (suitName == "spades") return {rank, "♠", QColor("#172018")};
    return {rank, "?", QColor("#172018")};
}

void drawCardFace(QPainter& painter, const QRectF& cardRect, const QString& description) {
    const auto card = compactCard(description);
    painter.save();
    painter.setPen(QPen(QColor("#C8A55B"), 2));
    painter.setBrush(QColor("#FFF9EA"));
    painter.drawRoundedRect(cardRect, 7, 7);
    painter.setPen(card.color);
    const auto rankSize = std::max(9, static_cast<int>(cardRect.height() * 0.22));
    const auto suitSize = std::max(16, static_cast<int>(cardRect.height() * 0.46));
    painter.setFont(QFont("Helvetica", rankSize, QFont::Bold));
    painter.drawText(cardRect.adjusted(4, 3, -3, -3), Qt::AlignLeft | Qt::AlignTop, card.rank);
    painter.setFont(QFont("Helvetica", suitSize, QFont::DemiBold));
    painter.drawText(cardRect.adjusted(2, cardRect.height() * 0.13, -2, 0), Qt::AlignCenter, card.suit);
    painter.restore();
}

class PokerTable final : public QWidget {
public:
    explicit PokerTable(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(minimumTableWidth, minimumTableHeight);
        setAccessibleName("Poker table");
    }

    void setPlayers(const QJsonArray& players) {
        hasTableState_ = false;
        playerNames_.fill({});
        playerStacks_.fill(0);
        playerCommitted_.fill(0);
        playerRoundCommitted_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
        playerFolded_.fill(false);
        playerSmallBlind_.fill(false);
        playerBigBlind_.fill(false);
        playerHoleCards_.fill({});
        playerShowdownDescriptions_.fill({});
        clearActionBoxes();
        presentedActingSeat_ = 0;
        playerPotWinnings_.fill(0);
        playerNetWinnings_.fill(0);
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
        hasTableState_ = false;
        playerNames_.fill({});
        playerStacks_.fill(0);
        playerCommitted_.fill(0);
        playerRoundCommitted_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
        playerFolded_.fill(false);
        playerSmallBlind_.fill(false);
        playerBigBlind_.fill(false);
        playerHoleCards_.fill({});
        playerShowdownDescriptions_.fill({});
        clearActionBoxes();
        playerPotWinnings_.fill(0);
        playerNetWinnings_.fill(0);
        pot_ = 0;
        currentBet_ = 0;
        street_.clear();
        communityCards_.clear();
        localHoleCards_.clear();
        showdownOccurred_ = false;
        presentedActingSeat_ = 0;
        update();
    }

    void setTableView(const QJsonObject& table) {
        const auto previousStreet = street_;
        hasTableState_ = true;
        playerNames_.fill({});
        playerStacks_.fill(0);
        playerCommitted_.fill(0);
        playerRoundCommitted_.fill(0);
        playerActing_.fill(false);
        playerDealer_.fill(false);
        playerFolded_.fill(false);
        playerSmallBlind_.fill(false);
        playerBigBlind_.fill(false);
        playerHoleCards_.fill({});
        playerShowdownDescriptions_.fill({});
        presentedActingSeat_ = 0;
        playerPotWinnings_.fill(0);
        playerNetWinnings_.fill(0);
        for (const auto& item : table.value("players").toArray()) {
            const auto player = item.toObject();
            const auto seat = player.value("seat").toInt();
            if (seat >= 1 && seat <= static_cast<int>(playerNames_.size())) {
                const auto index = static_cast<std::size_t>(seat - 1);
                playerNames_[index] = player.value("name").toString();
                playerStacks_[index] = player.value("stack").toInteger();
                playerCommitted_[index] = player.value("committed").toInteger();
                playerRoundCommitted_[index] = player.value("roundCommitted").toInteger();
                playerActing_[index] = player.value("acting").toBool();
                playerDealer_[index] = player.value("dealer").toBool();
                playerFolded_[index] = player.value("folded").toBool();
                for (const auto& card : player.value("holeCards").toArray()) playerHoleCards_[index].append(card.toString());
                playerShowdownDescriptions_[index] = player.value("showdownDescription").toString();
            }
        }
        street_ = table.value("street").toString();
        if (!previousStreet.isEmpty() && previousStreet != street_) clearActionBoxes();
        pot_ = 0;
        for (const auto& item : table.value("pots").toArray()) pot_ += item.toObject().value("amount").toInteger();
        currentBet_ = table.value("currentBet").toInteger();
        showdownOccurred_ = table.value("showdownOccurred").toBool();
        const auto smallBlindSeat = table.value("smallBlindSeat").toInt();
        const auto bigBlindSeat = table.value("bigBlindSeat").toInt();
        if (smallBlindSeat >= 1 && smallBlindSeat <= static_cast<int>(playerSmallBlind_.size())) playerSmallBlind_[smallBlindSeat - 1] = true;
        if (bigBlindSeat >= 1 && bigBlindSeat <= static_cast<int>(playerBigBlind_.size())) playerBigBlind_[bigBlindSeat - 1] = true;
        communityCards_.clear();
        for (const auto& card : table.value("communityCards").toArray()) communityCards_.append(card.toString());
        localHoleCards_.clear();
        if (!localPlayerName_.isEmpty()) {
            for (const auto& item : table.value("players").toArray()) {
                const auto player = item.toObject();
                if (player.value("name").toString() != localPlayerName_) continue;
                const auto localSeat = player.value("seat").toInt();
                if (localSeat >= 1 && localSeat <= static_cast<int>(playerHoleCards_.size())) {
                    localHoleCards_ = playerHoleCards_[static_cast<std::size_t>(localSeat - 1)];
                }
                break;
            }
        }
        for (const auto& item : table.value("payouts").toArray()) {
            for (const auto& awardItem : item.toObject().value("awards").toArray()) {
                const auto award = awardItem.toObject();
                const auto seat = award.value("seat").toInt();
                const auto amount = award.value("amount").toInteger();
                if (seat < 1 || seat > static_cast<int>(playerPotWinnings_.size())) continue;
                playerPotWinnings_[static_cast<std::size_t>(seat - 1)] += amount;
            }
        }
        for (std::size_t index = 0; index < playerPotWinnings_.size(); ++index) {
            playerNetWinnings_[index] = playerPotWinnings_[index] - playerCommitted_[index];
        }
        update();
    }

    void beginAutomatedActionReplay(bool newHand) {
        playerPotWinnings_.fill(0);
        playerNetWinnings_.fill(0);
        showdownOccurred_ = false;
        if (newHand) {
            street_ = "Preflop";
            communityCards_.clear();
            playerRoundCommitted_.fill(0);
            playerActing_.fill(false);
            playerFolded_.fill(false);
            pot_ = 0;
            currentBet_ = 0;
        }
        update();
    }

    void resetForRestart(qint64 startingStack) {
        for (std::size_t index = 0; index < playerNames_.size(); ++index) {
            if (!playerNames_[index].isEmpty()) playerStacks_[index] = startingStack;
        }
        playerCommitted_.fill(0);
        playerRoundCommitted_.fill(0);
        playerActing_.fill(false);
        playerFolded_.fill(false);
        playerPotWinnings_.fill(0);
        playerNetWinnings_.fill(0);
        clearActionBoxes();
        communityCards_.clear();
        pot_ = 0;
        currentBet_ = 0;
        street_ = "Preflop";
        showdownOccurred_ = false;
        presentedActingSeat_ = 0;
        update();
    }

    void showCommunityCards(QStringList cards) {
        const auto previousStreet = street_;
        communityCards_ = std::move(cards);
        playerRoundCommitted_.fill(0);
        street_ = communityCards_.size() >= 5 ? "River"
            : communityCards_.size() == 4 ? "Turn"
            : communityCards_.size() == 3 ? "Flop" : "Preflop";
        if (previousStreet != street_) clearActionBoxes();
        update();
    }

    void setLocalPlayerName(QString name) {
        localPlayerName_ = std::move(name);
        localHoleCards_.clear();
        update();
    }

    [[nodiscard]] QStringList localHoleCards() const { return localHoleCards_; }

    [[nodiscard]] QString localShowdownDescription() const {
        if (street_ != "Showdown") return {};
        const auto local = std::ranges::find(playerNames_, localPlayerName_);
        if (local == playerNames_.end()) return {};
        return playerShowdownDescriptions_[static_cast<std::size_t>(std::distance(playerNames_.begin(), local))];
    }

    void setDealerAdvanceHandler(std::function<void()> handler) { dealerAdvanceHandler_ = std::move(handler); }

    void clearActionBoxes() {
        for (auto& action : playerLastActions_) action = {};
        update();
    }

    void showLastAction(std::size_t seat, QString action, qint64 amount) {
        if (seat == 0 || seat > playerLastActions_.size()) return;
        if (action.compare("Fold", Qt::CaseInsensitive) == 0) playerFolded_[seat - 1] = true;
        playerLastActions_[seat - 1] = {.name = std::move(action), .amount = amount, .visible = true};
        update();
    }

    void showActionState(qint64 pot, qint64 currentBet) {
        pot_ = pot;
        currentBet_ = currentBet;
        update();
    }

    void showPlayerStack(std::size_t seat, qint64 stack) {
        if (seat == 0 || seat > playerStacks_.size()) return;
        playerStacks_[seat - 1] = stack;
        update();
    }

    void setPresentedActingSeat(std::size_t seat) {
        presentedActingSeat_ = seat > playerNames_.size() ? 0 : seat;
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
        const std::array<QPointF, defaultTableSeats> seats{{
            {width() * .25, felt.top()}, {width() * .50, felt.top()}, {width() * .75, felt.top()},
            {felt.right(), felt.top() + felt.height() * .32}, {felt.right(), felt.top() + felt.height() * .68},
            {width() * .75, felt.bottom()}, {width() * .50, felt.bottom()}, {width() * .25, felt.bottom()},
            {felt.left(), felt.top() + felt.height() * .68}, {felt.left(), felt.top() + felt.height() * .32},
        }};
        constexpr std::array<QPointF, defaultTableSeats> inwardDirections{{
            {0, 1}, {0, 1}, {0, 1}, {-1, 0}, {-1, 0}, {0, -1}, {0, -1}, {0, -1}, {1, 0}, {1, 0},
        }};
        constexpr std::array<QPointF, defaultTableSeats> tangents{{
            {1, 0}, {1, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 0}, {1, 0}, {0, 1}, {0, 1},
        }};
        constexpr std::array<QPointF, defaultTableSeats> actionDirections{{
            {-1, 0}, {-1, 0}, {-1, 0}, {0, -1}, {0, -1}, {1, 0}, {1, 0}, {1, 0}, {0, 1}, {0, 1},
        }};
        painter.setFont(QFont("Helvetica", 12));
        for (std::size_t i = 0; i < seats.size(); ++i) {
            const auto point = seats[i];
            const auto busted = hasTableState_ && !playerNames_[i].isEmpty() && playerStacks_[i] == 0
                && (street_ == "Showdown" || playerCommitted_[i] == 0);
            const auto folded = playerFolded_[i] && !busted;
            painter.setBrush(busted ? QColor("#777777") : folded ? QColor("#454545") : QColor("#162D24"));
            const auto localPlayer = !localPlayerName_.isEmpty() && playerNames_[i] == localPlayerName_;
            painter.setPen(QPen(localPlayer ? QColor("#F6D365") : Qt::white, localPlayer ? 3 : 1));
            painter.drawEllipse(point, playerRadius, playerRadius);
            const auto acting = presentedActingSeat_ == 0 ? playerActing_[i] : presentedActingSeat_ == i + 1;
            if (acting) {
                // Keep the action-history box factual: the double ring alone
                // identifies the player currently expected to act.
                painter.setPen(QPen(QColor("#F6D365"), 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(point, playerRadius - 6, playerRadius - 6);
            }
            const auto label = playerNames_[i].isEmpty()
                ? "Seat " + QString::number(i + 1)
                : playerNames_[i];
            painter.setFont(QFont("Helvetica", 12));
            painter.drawText(QRectF(point.x() - 58, point.y() - 35, 116, 22), Qt::AlignCenter, label);
            if (!playerNames_[i].isEmpty()) {
                painter.setFont(QFont("Helvetica", 13, QFont::DemiBold));
                painter.drawText(QRectF(point.x() - 58, point.y() - 11, 116, 22), Qt::AlignCenter, QString::number(playerStacks_[i]));
            }
        }

        drawBoard(painter, felt);
        dealerButtonRect_ = {};
        for (std::size_t i = 0; i < seats.size(); ++i) {
            if (playerNames_[i].isEmpty()) continue;
            const auto point = seats[i];
            const auto inward = inwardDirections[i];
            const auto buttonArea = point + inward * buttonAreaDistance;
            const auto tangent = tangents[i];
            const auto blind = playerSmallBlind_[i] ? RoleButton::smallBlind
                : playerBigBlind_[i] ? RoleButton::bigBlind : RoleButton::none;
            const auto busted = playerStacks_[i] == 0 && (street_ == "Showdown" || playerCommitted_[i] == 0);
            if (playerFolded_[i] && !busted) drawFoldedMarker(painter, point + QPointF(0, 25));
            if (playerDealer_[i] && blind != RoleButton::none) {
                const auto dealerButton = drawRoleButton(painter, buttonArea - tangent * 17, RoleButton::dealer);
                drawRoleButton(painter, buttonArea + tangent * 17, blind);
                if (street_ == "Showdown") dealerButtonRect_ = dealerButton;
            } else if (playerDealer_[i]) {
                const auto dealerButton = drawRoleButton(painter, buttonArea, RoleButton::dealer);
                if (street_ == "Showdown") dealerButtonRect_ = dealerButton;
            } else if (blind != RoleButton::none) {
                drawRoleButton(painter, buttonArea, blind);
            }
            if (street_ != "Showdown") {
                drawPendingBet(painter, buttonArea + inward * (buttonRadius + 25.0), playerRoundCommitted_[i]);
            }
            const auto actionDirection = actionDirections[i];
            const auto actionDistance = 45.0 + std::abs(actionDirection.x()) * 31.0 + std::abs(actionDirection.y()) * 19.0;
            const auto winningsDistance = 45.0 + std::abs(actionDirection.x()) * 35.0 + std::abs(actionDirection.y()) * 20.0;
            drawActionBox(painter, point + actionDirection * actionDistance, playerLastActions_[i]);
            if (street_ == "Showdown") {
                drawResultBox(painter, point - actionDirection * winningsDistance, playerPotWinnings_[i], playerNetWinnings_[i]);
            }
            if (showdownOccurred_ && (!playerFolded_[i] || playerNames_[i] == localPlayerName_) && !playerHoleCards_[i].isEmpty()) {
                drawPlayerCards(painter, playerHoleCards_[i], playerShowdownDescriptions_[i], point, inward,
                    isSouthSeat(i), 38, 52);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!dealerButtonRect_.isNull() && dealerButtonRect_.contains(event->position()) && dealerAdvanceHandler_) {
            dealerAdvanceHandler_();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    // The layout is deliberately cardinal rather than radial.  960 x 880 is the
    // smallest canvas that keeps the two east and west seats 284 px apart and
    // leaves their action, winnings, card, and description zones unobstructed.
    // It also retains clearance around the board and north/south player zones.
    static constexpr int minimumTableWidth = 960;
    static constexpr int minimumTableHeight = 880;
    static constexpr qreal playerRadius = 43.0;
    static constexpr qreal buttonRadius = 17.0;
    static constexpr qreal buttonAreaDistance = playerRadius + buttonRadius + 2.0;
    static constexpr qreal cardsAfterButtonsGap = 18.0;
    static constexpr qreal cardsDescriptionGap = 12.0;
    static constexpr qreal showdownDescriptionHeight = 28.0;

    struct ActionBox {
        QString name;
        qint64 amount{0};
        bool visible{false};
    };

    enum class RoleButton { none, dealer, smallBlind, bigBlind };

    [[nodiscard]] static QString chips(qint64 amount) { return QLocale().toString(amount); }

    static void drawCardRow(QPainter& painter, const QStringList& cards, QPointF center, qreal width, qreal height) {
        constexpr qreal gap = 8;
        const auto totalWidth = cards.size() * width + std::max<qsizetype>(0, cards.size() - 1) * gap;
        auto left = center.x() - totalWidth / 2.0;
        for (const auto& card : cards) {
            drawCardFace(painter, {left, center.y() - height / 2.0, width, height}, card);
            left += width + gap;
        }
    }

    static void drawShowdownDescription(QPainter& painter, const QString& description, const QPointF& center) {
        if (description.isEmpty()) return;
        painter.save();
        painter.setPen(Qt::white);
        painter.setFont(QFont("Helvetica", 12, QFont::DemiBold));
        painter.drawText(QRectF(center.x() - 100, center.y() - showdownDescriptionHeight / 2.0, 200,
                             showdownDescriptionHeight),
            Qt::AlignCenter | Qt::TextWordWrap, description);
        painter.restore();
    }

    [[nodiscard]] static bool isSouthSeat(std::size_t seatIndex) {
        return seatIndex >= 5 && seatIndex <= 7;
    }

    [[nodiscard]] static QPointF cardsCenter(const QPointF& playerCenter, const QPointF& inward,
        const QStringList& cards, qreal cardWidth, qreal cardHeight) {
        constexpr qreal cardGap = 8.0;
        const auto rowWidth = cards.size() * cardWidth + std::max<qsizetype>(0, cards.size() - 1) * cardGap;
        const auto inwardHalfExtent = std::abs(inward.x()) > 0.5 ? rowWidth / 2.0 : cardHeight / 2.0;
        const auto buttonCenter = playerCenter + inward * buttonAreaDistance;
        return buttonCenter + inward * (buttonRadius + cardsAfterButtonsGap + inwardHalfExtent);
    }

    static void drawPlayerCards(QPainter& painter, const QStringList& cards, const QString& description,
        const QPointF& playerCenter, const QPointF& inward, bool southSeat, qreal cardWidth, qreal cardHeight) {
        if (cards.isEmpty()) return;
        const auto center = cardsCenter(playerCenter, inward, cards, cardWidth, cardHeight);
        drawCardRow(painter, cards, center, cardWidth, cardHeight);
        if (!description.isEmpty()) {
            const auto descriptionOffset = cardHeight / 2.0 + cardsDescriptionGap + showdownDescriptionHeight / 2.0;
            const auto descriptionCenter = center + QPointF(0, southSeat ? -descriptionOffset : descriptionOffset);
            drawShowdownDescription(painter, description, descriptionCenter);
        }
    }

    static QRectF drawRoleButton(QPainter& painter, QPointF center, RoleButton role) {
        const auto dealer = role == RoleButton::dealer;
        const auto fill = dealer ? QColor("#F9F9F6") : role == RoleButton::smallBlind ? QColor("#2B45C8") : QColor("#F2B928");
        const auto textColor = dealer ? QColor("#283229") : Qt::white;
        const auto text = dealer ? "DEALER" : role == RoleButton::smallBlind ? "SMALL\nBLIND" : "BIG\nBLIND";
        const auto diameter = dealer ? 31.0 : 34.0;
        const QRectF rect(center.x() - diameter / 2.0, center.y() - diameter / 2.0, diameter, diameter);
        painter.save();
        painter.setFont(QFont("Helvetica", dealer ? 6 : 6, QFont::Bold));
        painter.setPen(QPen(dealer ? QColor("#B5B5AE") : QColor("#EEDB8C"), 1));
        painter.setBrush(fill);
        painter.drawEllipse(rect);
        painter.setPen(textColor);
        painter.drawText(rect.adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        painter.restore();
        return rect;
    }

    void drawFoldedMarker(QPainter& painter, QPointF center) const {
        const QRectF target(center.x() - 21, center.y() - 14, 42, 28);
        if (!foldedHands_.isNull()) painter.drawPixmap(target.toRect(), foldedHands_);
        else {
            painter.save();
            painter.setPen(QColor("#523018"));
            painter.setBrush(QColor("#EED9C4"));
            painter.drawRoundedRect(target, 8, 8);
            painter.drawText(target, Qt::AlignCenter, "Folded");
            painter.restore();
        }
    }

    static void drawActionBox(QPainter& painter, QPointF center, const ActionBox& action) {
        if (!action.visible) return;
        const QRectF rect(center.x() - 31, center.y() - 19, 62, 38);
        painter.save();
        painter.setPen(QPen(QColor("#262626"), 1));
        painter.setBrush(QColor("#FFFDFC"));
        painter.drawRoundedRect(rect, 12, 12);
        painter.setPen(QColor("#171717"));
        painter.setFont(QFont("Helvetica", 10, QFont::DemiBold));
        const auto text = action.amount > 0 ? action.name + '\n' + chips(action.amount) : action.name;
        painter.drawText(rect.adjusted(3, 2, -3, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        painter.restore();
    }

    static void drawResultBox(QPainter& painter, QPointF center, qint64 winnings, qint64 net) {
        if (net == 0) return;
        const QRectF rect(center.x() - 35, center.y() - 20, 70, 40);
        painter.save();
        const auto won = net > 0;
        painter.setPen(QPen(won ? QColor("#4C3B00") : QColor("#5B1717"), 1));
        painter.setBrush(won ? QColor("#F5E400") : QColor("#E55A5A"));
        painter.drawRoundedRect(rect, 14, 14);
        painter.setPen(QColor("#171717"));
        painter.setFont(QFont("Helvetica", 10, QFont::Bold));
        const auto text = won ? chips(winnings) + '\n' + "+" + chips(net)
            : "Lost\n-" + chips(-net);
        painter.drawText(rect.adjusted(3, 2, -3, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        painter.restore();
    }

    static void drawPendingBet(QPainter& painter, QPointF center, qint64 amount) {
        if (amount <= 0) return;
        const QRectF rect(center.x() - 34, center.y() - 14, 68, 28);
        painter.save();
        painter.setPen(QPen(QColor("#2E7D32"), 1));
        painter.setBrush(QColor("#B7E4C7"));
        painter.drawRoundedRect(rect, 12, 12);
        painter.setPen(QColor("#171717"));
        painter.setFont(QFont("Helvetica", 11, QFont::Bold));
        painter.drawText(rect.adjusted(4, 2, -4, -2), Qt::AlignCenter, chips(amount));
        painter.restore();
    }

    void drawBoard(QPainter& painter, const QRectF& felt) const {
        painter.save();
        painter.setPen(Qt::white);
        painter.setFont(QFont("Helvetica", 18, QFont::DemiBold));
        const auto header = "POT  " + chips(pot_) + "\nCurrent bet: " + chips(currentBet_);
        painter.drawText(QRectF(felt.center().x() - 200, felt.center().y() - 90, 400, 52), Qt::AlignCenter, header);
        if (communityCards_.isEmpty()) {
            painter.setFont(QFont("Helvetica", 13));
            painter.drawText(QRectF(felt.center().x() - 200, felt.center().y() - 18, 400, 30), Qt::AlignCenter, "Community cards will appear here");
        } else {
            drawCardRow(painter, communityCards_, {felt.center().x(), felt.center().y() + 22}, 54, 74);
        }
        painter.restore();
    }

    std::array<QString, defaultTableSeats> playerNames_{};
    std::array<qint64, defaultTableSeats> playerStacks_{};
    std::array<qint64, defaultTableSeats> playerCommitted_{};
    std::array<qint64, defaultTableSeats> playerRoundCommitted_{};
    std::array<bool, defaultTableSeats> playerActing_{};
    std::array<bool, defaultTableSeats> playerDealer_{};
    std::array<bool, defaultTableSeats> playerFolded_{};
    std::array<bool, defaultTableSeats> playerSmallBlind_{};
    std::array<bool, defaultTableSeats> playerBigBlind_{};
    std::array<QStringList, defaultTableSeats> playerHoleCards_{};
    std::array<QString, defaultTableSeats> playerShowdownDescriptions_{};
    std::array<ActionBox, defaultTableSeats> playerLastActions_{};
    std::array<qint64, defaultTableSeats> playerPotWinnings_{};
    std::array<qint64, defaultTableSeats> playerNetWinnings_{};
    std::size_t presentedActingSeat_{0};
    QString localPlayerName_;
    QStringList communityCards_;
    QStringList localHoleCards_;
    qint64 pot_{0};
    qint64 currentBet_{0};
    QString street_;
    bool showdownOccurred_{};
    bool hasTableState_{};
    QPixmap foldedHands_{":/bluffskill/resources/folded_hands.png"};
    QRectF dealerButtonRect_;
    std::function<void()> dealerAdvanceHandler_;
};

class LocalHoleCardsWidget final : public QWidget {
public:
    explicit LocalHoleCardsWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setVisible(false);
        setAccessibleName("Your hole cards");
    }

    void setCards(QStringList cards, QString description = {}) {
        cards_ = std::move(cards);
        description_ = std::move(description);
        const auto showingDescription = !description_.isEmpty();
        setFixedSize(showingDescription ? 240 : 108, showingDescription ? 98 : 68);
        setVisible(!cards_.isEmpty());
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (cards_.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        constexpr qreal cardWidth = 46.0;
        constexpr qreal cardHeight = 64.0;
        constexpr qreal gap = 8.0;
        const auto rowWidth = cards_.size() * cardWidth + std::max<qsizetype>(0, cards_.size() - 1) * gap;
        auto left = (width() - rowWidth) / 2.0;
        for (const auto& card : cards_) {
            drawCardFace(painter, {left, 2.0, cardWidth, cardHeight}, card);
            left += cardWidth + gap;
        }
        if (!description_.isEmpty()) {
            painter.setPen(QColor("#172018"));
            painter.setFont(QFont("Helvetica", 11, QFont::DemiBold));
            painter.drawText(QRectF(3, 70, width() - 6, 25), Qt::AlignCenter | Qt::TextWordWrap, description_);
        }
    }

private:
    QStringList cards_;
    QString description_;
};

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

class ConnectionDialog final : public QDialog {
public:
    explicit ConnectionDialog(const bluffskill::app_config::Settings& settings, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Connect to BluffSkill Server");
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        host_ = new QLineEdit("127.0.0.1", this);
        host_->setPlaceholderText("localhost or a server name");
        port_ = new QLineEdit(QString::number(settings.serverPreferredPorts.value(0)), this);
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
        port_->setFocus();
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

struct CustomGameConfiguration {
    bool includeHuman{true};
    QString playerName;
    int leoPlayers{5};
    int virgoPlayers{4};
};

class CustomGameDialog final : public QDialog {
public:
    explicit CustomGameDialog(const QString& defaultPlayerName, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Custom Game");
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel("Choose exactly 10 players for this table.", this));
        auto* form = new QFormLayout;
        includeHuman_ = new QCheckBox("Include local human player", this);
        includeHuman_->setChecked(true);
        playerName_ = new QLineEdit(defaultPlayerName, this);
        leoPlayers_ = new QSpinBox(this); leoPlayers_->setRange(0, defaultTableSeats); leoPlayers_->setValue(5);
        virgoPlayers_ = new QSpinBox(this); virgoPlayers_->setRange(0, defaultTableSeats); virgoPlayers_->setValue(4);
        total_ = new QLabel(this);
        form->addRow("Human player", includeHuman_);
        form->addRow("Player name", playerName_);
        form->addRow("Leo players", leoPlayers_);
        form->addRow("Virgo players", virgoPlayers_);
        form->addRow("Total", total_);
        layout->addLayout(form);
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] { accept(); });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(includeHuman_, &QCheckBox::toggled, this, [this] { updateState(); });
        connect(playerName_, &QLineEdit::textChanged, this, [this] { updateState(); });
        connect(leoPlayers_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateState(); });
        connect(virgoPlayers_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateState(); });
        layout->addWidget(buttons_);
        updateState();
    }

    [[nodiscard]] CustomGameConfiguration configuration() const {
        return {.includeHuman = includeHuman_->isChecked(), .playerName = playerName_->text().trimmed(),
            .leoPlayers = leoPlayers_->value(), .virgoPlayers = virgoPlayers_->value()};
    }

private:
    void updateState() {
        playerName_->setEnabled(includeHuman_->isChecked());
        const auto total = (includeHuman_->isChecked() ? 1 : 0) + leoPlayers_->value() + virgoPlayers_->value();
        const auto valid = total == defaultTableSeats && (!includeHuman_->isChecked() || !playerName_->text().trimmed().isEmpty());
        total_->setText(QString::number(total) + " / " + QString::number(defaultTableSeats));
        total_->setStyleSheet(valid ? QString{} : QStringLiteral("color: #B00020;"));
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);
    }

    QCheckBox* includeHuman_{};
    QLineEdit* playerName_{};
    QSpinBox* leoPlayers_{};
    QSpinBox* virgoPlayers_{};
    QLabel* total_{};
    QDialogButtonBox* buttons_{};
};

class ClientWindow final : public QMainWindow {
public:
    ClientWindow() : settings_(bluffskill::app_config::AppConfig::load()) {
        setWindowTitle("BluffSkill");
        resize(1200, 1200);
        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        auto* connection = new QFrame(central);
        auto* connectionLayout = new QVBoxLayout(connection);
        connectionLayout->setContentsMargins(0, 0, 0, 0);
        const auto readOnlyField = [connection](const QString& widestValue) {
            auto* field = new QLineEdit(connection);
            field->setReadOnly(true);
            field->setFocusPolicy(Qt::NoFocus);
            field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            field->setFixedWidth(field->fontMetrics().horizontalAdvance(widestValue) + 18);
            return field;
        };
        auto* connectionRow = new QHBoxLayout;
        server_ = new QComboBox(connection); server_->addItem("Not connected"); server_->setEnabled(false);
        competition_ = new QComboBox(connection); competition_->addItem("Choose a server first"); competition_->setEnabled(false);
        table_ = new QComboBox(connection); table_->addItem("Choose a competition first"); table_->setEnabled(false);
        connectionRow->addWidget(new QLabel("Server", connection));
        connectionRow->addWidget(server_, 1);
        connectionRow->addSpacing(14);
        connectionRow->addWidget(new QLabel("Competition", connection));
        connectionRow->addWidget(competition_, 1);
        connectionRow->addSpacing(14);
        connectionRow->addWidget(new QLabel("Table", connection));
        connectionRow->addWidget(table_, 1);
        pausePlayButton_ = new QPushButton(connection);
        pausePlayButton_->setFixedWidth(38);
        pausePlayButton_->setEnabled(false);
        setPausePlayButtonMode(false);
        connect(pausePlayButton_, &QPushButton::clicked, this, [this] { togglePause(); });
        connectionRow->addSpacing(8);
        connectionRow->addWidget(pausePlayButton_);
        connectionLayout->addLayout(connectionRow);
        auto* dealRow = new QHBoxLayout;
        remainingPlayersCaption_ = new QLabel("Remaining Players:", connection);
        dealRow->addWidget(remainingPlayersCaption_);
        remainingPlayers_ = readOnlyField("Player-Name");
        remainingPlayers_->setText("—");
        dealRow->addWidget(remainingPlayers_);
        dealRow->addSpacing(20);
        dealRow->addWidget(new QLabel("Blinds:", connection));
        smallBlindAmount_ = readOnlyField("9,999,999");
        smallBlindAmount_->setText("—");
        dealRow->addWidget(smallBlindAmount_);
        dealRow->addWidget(new QLabel("/", connection));
        bigBlindAmount_ = readOnlyField("9,999,999");
        bigBlindAmount_->setText("—");
        dealRow->addWidget(bigBlindAmount_);
        dealRow->addSpacing(20);
        dealRow->addWidget(new QLabel("Rounds Played:", connection));
        roundsPlayed_ = readOnlyField("9,999");
        roundsPlayed_->setText("—");
        dealRow->addWidget(roundsPlayed_);
        dealRow->addSpacing(20);
        dealRow->addWidget(new QLabel("Next Deal In", connection));
        nextDealIn_ = readOnlyField("3,600");
        nextDealIn_->setText("—");
        dealRow->addWidget(nextDealIn_);
        dealRow->addSpacing(12);
        dealNowButton_ = new QPushButton("Deal Now", connection);
        dealNowButton_->setEnabled(false);
        connect(dealNowButton_, &QPushButton::clicked, this, [this] { startNextHand(); });
        dealRow->addWidget(dealNowButton_);
        dealRow->addStretch(1);
        connectionLayout->addLayout(dealRow);
        layout->addWidget(connection);
        pokerTable_ = new PokerTable(central);
        pokerTable_->setDealerAdvanceHandler([this] { startNextHand(); });
        layout->addWidget(pokerTable_, 1);
        auto* actions = new QFrame(central);
        auto* actionLayout = new QVBoxLayout(actions);
        auto* actionInfoLayout = new QHBoxLayout;
        actionClockCaption_ = new QLabel("Action Clock:", actions);
        actionClockValue_ = new QLineEdit(actions);
        actionClockValue_->setReadOnly(true);
        actionClockValue_->setFocusPolicy(Qt::NoFocus);
        actionClockValue_->setFixedWidth(actionClockValue_->fontMetrics().horizontalAdvance("3600"));
        actionClockValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        actionClockCaption_->setVisible(false);
        actionClockValue_->setVisible(false);
        actionInfoLayout->addWidget(actionClockCaption_);
        actionInfoLayout->addWidget(actionClockValue_);
        callAmountCaption_ = new QLabel("Amount to call:", actions);
        callAmountValue_ = new QLineEdit(actions);
        callAmountValue_->setReadOnly(true);
        callAmountValue_->setFocusPolicy(Qt::NoFocus);
        callAmountValue_->setFixedWidth(callAmountValue_->fontMetrics().horizontalAdvance("9,999,999") + 18);
        callAmountValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        minimumWagerCaption_ = new QLabel("Minimum Raise/Bet:", actions);
        minimumWagerValue_ = new QLineEdit(actions);
        minimumWagerValue_->setReadOnly(true);
        minimumWagerValue_->setFocusPolicy(Qt::NoFocus);
        minimumWagerValue_->setFixedWidth(minimumWagerValue_->fontMetrics().horizontalAdvance("9,999,999") + 18);
        minimumWagerValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setActionDetailsVisible(false);
        actionInfoLayout->addSpacing(18);
        actionInfoLayout->addWidget(callAmountCaption_);
        actionInfoLayout->addWidget(callAmountValue_);
        actionInfoLayout->addSpacing(12);
        actionInfoLayout->addWidget(minimumWagerCaption_);
        actionInfoLayout->addWidget(minimumWagerValue_);
        actionInfoLayout->addStretch(1);
        actionLayout->addLayout(actionInfoLayout);
        auto* actionControlsLayout = new QHBoxLayout;
        for (const auto* action : {"Check", "Call", "Bet", "Raise", "Fold"}) {
            auto* button = new QPushButton(action, actions);
            button->setProperty("actionName", QString::fromUtf8(action).toLower());
            button->setEnabled(false);
            actionButtons_.push_back(button);
            connect(button, &QPushButton::clicked, this, [this, action] { selectHumanAction(action); });
            actionControlsLayout->addWidget(button);
        }
        actionLayout->addLayout(actionControlsLayout);
        auto* wagerLayout = new QHBoxLayout;
        wagerLayout->addWidget(new QLabel("Bet Amount", actions));
        amountEdit_ = new QLineEdit(actions);
        amountEdit_->setValidator(new QIntValidator(0, 1000000000, amountEdit_));
        amountEdit_->setEnabled(false);
        amountEdit_->setMinimumWidth(115);
        connect(amountEdit_, &QLineEdit::editingFinished, this, [this] { normalizeWagerAmount(); });
        connect(amountEdit_, &QLineEdit::textChanged, this, [this] { updateWagerSummary(wagerRaiseAvailable_, wagerCurrentBet_); });
        wagerLayout->addWidget(amountEdit_);
        denominationControls_ = new QWidget(actions);
        denominationLayout_ = new QHBoxLayout(denominationControls_);
        denominationLayout_->setContentsMargins(0, 0, 0, 0);
        denominationLayout_->setSpacing(6);
        wagerLayout->addWidget(denominationControls_);
        wagerLayout->addSpacing(16);
        wagerLayout->addWidget(new QLabel("Total commitment", actions));
        totalCommitment_ = new QLineEdit(actions);
        totalCommitment_->setReadOnly(true);
        totalCommitment_->setFocusPolicy(Qt::NoFocus);
        totalCommitment_->setFixedWidth(totalCommitment_->fontMetrics().horizontalAdvance("9,999,999") + 18);
        totalCommitment_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        totalCommitment_->setText("—");
        wagerLayout->addWidget(totalCommitment_);
        raiseSummary_ = new QLabel(actions);
        wagerLayout->addWidget(raiseSummary_);
        wagerLayout->addStretch(1);
        localHoleCardsWidget_ = new LocalHoleCardsWidget(actions);
        wagerLayout->addWidget(localHoleCardsWidget_);
        actionLayout->addLayout(wagerLayout);
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
        customGameAction_ = gameMenu->addAction("Custom Game…");
        setNewGameActionsEnabled(false);
        connect(newGameAction_, &QAction::triggered, this, [this] { newGame(); });
        connect(customGameAction_, &QAction::triggered, this, [this] { customGame(); });
        restartGameAction_ = gameMenu->addAction("Restart Game");
        restartGameAction_->setEnabled(false);
        connect(restartGameAction_, &QAction::triggered, this, [this] { restartGame(); });
        auto* applicationMenu = menuBar()->addMenu("Application");
        auto* settingsAction = applicationMenu->addAction("Settings…");
        auto* aboutAction = applicationMenu->addAction("About BluffSkill");
        connect(settingsAction, &QAction::triggered, this, [this] { editSettings(); });
        connect(aboutAction, &QAction::triggered, this, [this] { showAbout(); });
        connect(competition_, &QComboBox::currentIndexChanged, this, [this] { refreshTables(); });
        connect(table_, &QComboBox::currentIndexChanged, this, [this] { refreshSeats(); });
        nextHandTimer_.setSingleShot(true);
        automatedActionTimer_.setSingleShot(true);
        connect(&nextHandTimer_, &QTimer::timeout, this, [this] { startNextHand(); });
        connect(&nextDealCountdownTimer_, &QTimer::timeout, this, [this] { advanceNextDealCountdown(); });
        connect(&turnCountdownTimer_, &QTimer::timeout, this, [this] { advanceTurnCountdown(); });
        connect(&automatedActionTimer_, &QTimer::timeout, this, [this] { presentNextAutomatedAction(); });
        statusBar()->showMessage("Choose Connection → Connect… to begin.");
        if (settings_.clientAutoConnect) QTimer::singleShot(0, this, [this] { autoConnectNext(); });
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

    void editSettings() {
        SettingsDialog dialog(settings_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        settings_ = dialog.settings(settings_);
        bluffskill::app_config::AppConfig::save(settings_);
        statusBar()->showMessage("Settings saved. New clock values apply to the next countdown.");
    }

    void showAbout() {
        QMessageBox::about(this, "About BluffSkill",
            "BluffSkill\nVersion: " + QStringLiteral(BLUFFSKILL_BUILD_VERSION)
                + "\nBuild number: " + QStringLiteral(BLUFFSKILL_BUILD_NUMBER)
                + "\nBuild timestamp: " + QStringLiteral(BLUFFSKILL_BUILD_TIMESTAMP)
                + "\n\n© AzoneLayer · azonelayer.com\nLicensed under the MIT License.");
    }

    void setNewGameActionsEnabled(bool enabled) {
        newGameAction_->setEnabled(enabled);
        customGameAction_->setEnabled(enabled);
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
        setNewGameActionsEnabled(false);
        restartGameAction_->setEnabled(false);
    }

    void connectToServer() {
        autoConnectInProgress_ = false;
        ConnectionDialog dialog(settings_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        connectToEndpoint(dialog.serverUrl(), dialog.displayAddress(), false);
    }

    void connectToEndpoint(QUrl baseUrl, const QString& address, bool automatic) {
        const auto attempt = ++connectionGeneration_;
        if (healthReply_) healthReply_->abort();
        setNewGameActionsEnabled(false);
        statusBar()->showMessage("Connecting to " + address + "…");
        auto healthUrl = baseUrl;
        healthUrl.setPath("/v1/health");
        auto* reply = track(network_.get(QNetworkRequest(healthUrl)));
        healthReply_ = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, address, baseUrl, attempt, automatic] {
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
                if (automatic && autoConnectInProgress_) {
                    autoConnectNext();
                    return;
                }
                statusBar()->showMessage("Could not connect to " + address);
                const auto detail = status > 0 ? "The server returned HTTP " + QString::number(status) + "." : error;
                QMessageBox::warning(this, "Server unavailable",
                    "BluffSkill could not verify the server at " + address + ".\n\n" + detail);
                setNewGameActionsEnabled(connected_);
                return;
            }

            connected_ = true;
            autoConnectInProgress_ = false;
            serverUrl_ = baseUrl;
            clearHumanPlayer();
            server_->setEnabled(true);
            server_->clear();
            server_->addItem(address);
            disconnectAction_->setEnabled(true);
            setNewGameActionsEnabled(true);
            restartGameAction_->setEnabled(true);
            statusBar()->showMessage("Connected to " + address);
            refreshCompetitions();
        });
    }

    void autoConnectNext() {
        if (!settings_.clientAutoConnect) return;
        if (!autoConnectInProgress_) {
            autoConnectInProgress_ = true;
            autoConnectPortIndex_ = 0;
        }
        if (autoConnectPortIndex_ >= settings_.serverPreferredPorts.size()) {
            autoConnectInProgress_ = false;
            statusBar()->showMessage("No preferred local server was found.");
            return;
        }
        const auto port = settings_.serverPreferredPorts.at(autoConnectPortIndex_++);
        QUrl url;
        url.setScheme("http");
        url.setHost("127.0.0.1");
        url.setPort(port);
        connectToEndpoint(url, "127.0.0.1:" + QString::number(port), true);
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
        autoConnectInProgress_ = false;
        stopNextDealCountdown();
        stopTurnCountdown();
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
                statusBar()->showMessage("Could not retrieve the current table state.");
                return;
            }
            applyTableView(view);
        });
    }

    void applyTableView(const QJsonObject& view, bool restartPresentation = false) {
        const auto previousStreet = lastStreet_;
        const auto deferShowdownForAutomatedActions = view.value("street").toString() == "Showdown"
            && (presentFinalAutomatedActions_ || humanPlayerBusted(view) || !humanPlayer_ || restartPresentation);
        if (deferShowdownForAutomatedActions) {
            const auto presentingAutomatedActions = queueNewActionBoxes(view, previousStreet, true, restartPresentation);
            presentFinalAutomatedActions_ = false;
            if (presentingAutomatedActions) {
                stopTurnCountdown();
                setActionControlsEnabled(false);
                setWagerControlsEnabled(false);
                return;
            }
        } else {
            presentFinalAutomatedActions_ = false;
        }
        pokerTable_->setTableView(view);
        presentedCommunityCards_ = communityCards(view);
        queuedCommunityCardCount_ = presentedCommunityCards_.size();
        localHoleCardsWidget_->setCards(view.value("street").toString() == "Showdown" ? QStringList{} : pokerTable_->localHoleCards());
        tableSequence_ = view.value("sequence").toInteger();
        lastStreet_ = view.value("street").toString();
        const auto presentingAutomatedActions = deferShowdownForAutomatedActions
            ? false : queueNewActionBoxes(view, previousStreet);
        int remaining = 0;
        int playersWithChips = 0;
        QString tableWinner;
        for (const auto& item : view.value("players").toArray()) {
            const auto player = item.toObject();
            if (player.value("stack").toInteger() > 0 || player.value("committed").toInteger() > 0) ++remaining;
            if (player.value("stack").toInteger() > 0) {
                ++playersWithChips;
                tableWinner = player.value("name").toString();
            }
        }
        tableComplete_ = view.value("street").toString() == "Showdown" && playersWithChips == 1;
        remainingPlayersCaption_->setText(tableComplete_ ? "Table Winner:" : "Remaining Players:");
        remainingPlayers_->setText(tableComplete_ ? tableWinner : QString::number(remaining));
        const auto legal = view.value("legalActions").toObject();
        setChipDenominations(view.value("chipDenominations").toArray());
        const auto currentBet = view.value("currentBet").toInteger();
        wagerCurrentBet_ = currentBet;
        const auto callAmount = legal.value("callAmount").toInteger();
        wagerExistingCommitment_ = 0;
        if (humanPlayer_) {
            for (const auto& item : view.value("players").toArray()) {
                const auto player = item.toObject();
                if (player.value("name").toString() == humanPlayer_->apiPlayerName()) {
                    wagerExistingCommitment_ = player.value("roundCommitted").toInteger();
                    break;
                }
            }
        }
        smallBlindAmount_->setText(QLocale().toString(view.value("smallBlind").toInteger()));
        bigBlindAmount_->setText(QLocale().toString(view.value("bigBlind").toInteger()));
        roundsPlayed_->setText(QLocale().toString(view.value("roundsPlayed").toInteger()));
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
        wagerMinimum_ = std::max<qint64>(0, minimum - wagerExistingCommitment_);
        wagerMaximum_ = std::max<qint64>(0, maximum - wagerExistingCommitment_);
        setWagerControlsEnabled(canSetAmount);
        if (canSetAmount) {
            if (!amountEdit_->hasFocus()) setWagerAmount(wagerMinimum_);
        } else {
            amountEdit_->clear();
        }
        updateWagerSummary(legal.value("raise").toBool() && canSetAmount, currentBet);
        if (presentingAutomatedActions) {
            stopTurnCountdown();
            setActionControlsEnabled(false);
            setWagerControlsEnabled(false);
            return;
        }
        const auto handFinished = view.value("street").toString() == "Showdown";
        if (handFinished) {
            stopTurnCountdown();
            if (tableComplete_) {
                stopNextDealCountdown();
                statusBar()->showMessage("Table winner: " + tableWinner + ". The game is complete. Choose Game → Restart Game to play again.");
                return;
            }
            const auto dealerDelayMilliseconds = !humanPlayer_
                ? settings_.uninterruptedDealerDelayMilliseconds
                : humanPlayerBusted(view) ? settings_.uninterruptedDealerDelayMilliseconds
                : settings_.dealClockSeconds * 1'000;
            if (lastShowdownSequence_ != tableSequence_) {
                lastShowdownSequence_ = tableSequence_;
                beginNextDealCountdown(dealerDelayMilliseconds);
            }
        } else {
            stopNextDealCountdown();
            lastShowdownSequence_ = -1;
            if (!humanPlayer_) statusBar()->showMessage("Choose a local player through New Game to view private cards and act.");
            else if (anyAction) {
                setActionDetails(callAmount, canSetAmount ? wagerMinimum_ : 0, canSetAmount);
                beginTurnCountdown(legal);
                statusBar()->clearMessage();
                setActionClockVisible(true);
            } else {
                stopTurnCountdown();
                statusBar()->showMessage("Waiting for " + view.value("actingSeat").toVariant().toString() + " to act.");
            }
        }
    }

    [[nodiscard]] static QString durationText(int milliseconds) {
        return QString::number(std::max(0, (milliseconds + 999) / 1'000)) + " s";
    }

    [[nodiscard]] static QString countdownValue(int milliseconds) {
        return QString::number(std::max(0, (milliseconds + 999) / 1'000));
    }

    [[nodiscard]] bool humanPlayerBusted(const QJsonObject& view) const {
        if (!humanPlayer_) return false;
        for (const auto& item : view.value("players").toArray()) {
            const auto player = item.toObject();
            if (player.value("name").toString() == humanPlayer_->apiPlayerName()) return player.value("stack").toInteger() == 0;
        }
        return false;
    }

    [[nodiscard]] static QString actionFingerprint(const QJsonObject& action) {
        return QString::number(action.value("seat").toInteger()) + '\x1f' + action.value("player").toString() + '\x1f'
            + action.value("street").toString() + '\x1f' + action.value("action").toString() + '\x1f'
            + QString::number(action.value("amount").toInteger()) + '\x1f' + QString::number(action.value("stackAfter").toInteger())
            + '\x1f' + QString::number(action.value("pot").toInteger()) + '\x1f' + QString::number(action.value("currentBet").toInteger());
    }

    [[nodiscard]] static QStringList communityCards(const QJsonObject& view) {
        QStringList cards;
        for (const auto& card : view.value("communityCards").toArray()) cards.append(card.toString());
        return cards;
    }

    void queueCommunityCardReveal(const QStringList& cards, qsizetype count, const QString& street) {
        if (queuedCommunityCardCount_ >= count || cards.size() < count) return;
        QStringList revealed;
        for (qsizetype index = 0; index < count; ++index) revealed.append(cards.at(index));
        automatedActionQueue_.append({.type = AutomatedActionPresentation::Type::dealerReveal,
            .player = "Dealer", .action = street, .communityCards = std::move(revealed)});
        queuedCommunityCardCount_ = count;
    }

    void queueCommunityCardsThrough(const QStringList& cards, qsizetype count) {
        if (count >= 3) queueCommunityCardReveal(cards, 3, "Flop");
        if (count >= 4) queueCommunityCardReveal(cards, 4, "Turn");
        if (count >= 5) queueCommunityCardReveal(cards, 5, "River");
    }

    bool queueNewActionBoxes(const QJsonObject& view, const QString& previousStreet, bool preserveShowdownAutomatedActions = false,
        bool resetForRestart = false) {
        const auto history = view.value("actionHistory").toArray();
        const auto players = view.value("players").toArray();
        QStringList fingerprints;
        fingerprints.reserve(history.size());
        for (const auto& item : history) fingerprints.append(actionFingerprint(item.toObject()));
        const auto continues = displayedActionHistory_.size() <= fingerprints.size()
            && std::equal(displayedActionHistory_.cbegin(), displayedActionHistory_.cend(), fingerprints.cbegin());
        const auto newHand = !continues
            || (previousStreet == "Showdown" && view.value("street").toString() == "Preflop");
        if (newHand) {
            displayedActionHistoryCount_ = 0;
            displayedActionHistory_.clear();
            automatedActionQueue_.clear();
            automatedActionTimer_.stop();
            pokerTable_->clearActionBoxes();
        }
        // A completed table normally begins its deal countdown immediately.
        // Once the local player has folded, gone all-in, or been eliminated,
        // the authoritative server can resolve several reference-player actions
        // in one response. Replay them before revealing the resulting showdown.
        if (view.value("street").toString() == "Showdown" && !preserveShowdownAutomatedActions) {
            displayedActionHistoryCount_ = history.size();
            displayedActionHistory_ = std::move(fingerprints);
            automatedActionQueue_.clear();
            automatedActionTimer_.stop();
            pokerTable_->setPresentedActingSeat(0);
            return false;
        }
        const auto cards = communityCards(view);
        if (preserveShowdownAutomatedActions) {
            if (resetForRestart) pokerTable_->resetForRestart(view.value("startingStack").toInteger());
            pokerTable_->beginAutomatedActionReplay(newHand);
            if (newHand) {
                presentedCommunityCards_.clear();
                queuedCommunityCardCount_ = 0;
            } else {
                queuedCommunityCardCount_ = presentedCommunityCards_.size();
            }
        }
        const auto firstUnpresentedAction = newHand ? qsizetype{0} : displayedActionHistoryCount_;
        if (preserveShowdownAutomatedActions && firstUnpresentedAction == 0) {
            for (const auto& blindValue : history) {
                const auto blind = blindValue.toObject();
                const auto actionName = blind.value("action").toString();
                if (actionName != "Small Blind" && actionName != "Big Blind") continue;
                pokerTable_->showPlayerStack(static_cast<std::size_t>(blind.value("seat").toInt()), blind.value("stackAfter").toInteger());
            }
            const auto bigBlind = std::ranges::find_if(history, [](const QJsonValue& value) {
                return value.toObject().value("action").toString() == "Big Blind";
            });
            if (bigBlind != history.end()) {
                const auto blind = bigBlind->toObject();
                pokerTable_->showActionState(blind.value("pot").toInteger(), blind.value("currentBet").toInteger());
            }
        }
        for (qsizetype index = firstUnpresentedAction; index < history.size(); ++index) {
            const auto action = history.at(index).toObject();
            if (preserveShowdownAutomatedActions) {
                const auto street = action.value("street").toString();
                if (street == "Flop") queueCommunityCardsThrough(cards, 3);
                else if (street == "Turn") queueCommunityCardsThrough(cards, 4);
                else if (street == "River") queueCommunityCardsThrough(cards, 5);
            }
            const auto actionName = action.value("action").toString();
            if (actionName == "Small Blind" || actionName == "Big Blind") continue;
            const auto seat = static_cast<std::size_t>(action.value("seat").toInt());
            const auto playerName = action.value("player").toString();
            const auto amount = action.value("amount").toInteger();
            const auto stackAfter = action.value("stackAfter").toInteger();
            const auto pot = action.value("pot").toInteger();
            const auto currentBet = action.value("currentBet").toInteger();
            const auto player = std::ranges::find_if(players, [seat](const QJsonValue& value) {
                return static_cast<std::size_t>(value.toObject().value("seat").toInt()) == seat;
            });
            const auto reference = player != players.end()
                && player->toObject().value("kind").toString() == "Reference";
            if (reference) automatedActionQueue_.append({.type = AutomatedActionPresentation::Type::playerAction,
                .seat = seat, .player = playerName, .action = actionName, .amount = amount, .stackAfter = stackAfter,
                .pot = pot, .currentBet = currentBet});
            else {
                pokerTable_->showLastAction(seat, actionName, amount);
                pokerTable_->showPlayerStack(seat, stackAfter);
                pokerTable_->showActionState(pot, currentBet);
            }
        }
        if (preserveShowdownAutomatedActions) queueCommunityCardsThrough(cards, cards.size());
        displayedActionHistoryCount_ = history.size();
        displayedActionHistory_ = std::move(fingerprints);
        if (automatedActionTimer_.isActive() || automatedActionQueue_.isEmpty()) return automatedActionTimer_.isActive();
        presentNextAutomatedAction();
        return true;
    }

    void presentNextAutomatedAction() {
        if (automatedActionQueue_.isEmpty()) {
            automatedActionTimer_.stop();
            pokerTable_->setPresentedActingSeat(0);
            refreshTableView();
            return;
        }
        const auto action = automatedActionQueue_.takeFirst();
        if (action.type == AutomatedActionPresentation::Type::dealerReveal) {
            pokerTable_->setPresentedActingSeat(0);
            pokerTable_->showCommunityCards(action.communityCards);
            presentedCommunityCards_ = action.communityCards;
            statusBar()->showMessage("Dealer deals the " + action.action.toLower() + ".");
        } else {
            pokerTable_->setPresentedActingSeat(action.seat);
            pokerTable_->showLastAction(action.seat, action.action, action.amount);
            pokerTable_->showPlayerStack(action.seat, action.stackAfter);
            pokerTable_->showActionState(action.pot, action.currentBet);
            statusBar()->showMessage(action.player + " " + action.action.toLower()
                + (action.amount > 0 ? " " + QLocale().toString(action.amount) : "") + ".");
        }
        automatedActionTimer_.start(settings_.automatedPlayerDelayMilliseconds);
    }

    void setWagerControlsEnabled(bool enabled) {
        amountEdit_->setEnabled(enabled);
        for (auto* button : denominationUp_) button->setEnabled(enabled);
        for (auto* button : denominationDown_) button->setEnabled(enabled);
    }

    void setActionClockVisible(bool visible) {
        actionClockCaption_->setVisible(visible);
        actionClockValue_->setVisible(visible);
        if (visible) actionClockValue_->setText(QString::number(turnSeconds_));
    }

    void setActionDetailsVisible(bool visible) {
        callAmountCaption_->setVisible(visible);
        callAmountValue_->setVisible(visible);
        minimumWagerCaption_->setVisible(visible);
        minimumWagerValue_->setVisible(visible);
    }

    void setActionDetails(qint64 callAmount, qint64 minimumWager, bool canRaiseOrBet) {
        callAmountValue_->setText(QLocale().toString(callAmount));
        minimumWagerValue_->setText(canRaiseOrBet ? QLocale().toString(minimumWager) : "—");
        setActionDetailsVisible(true);
    }

    void setPausePlayButtonMode(bool paused) {
        pausePlayButton_->setText(QString(paused ? QChar(0x25B6) : QChar(0x23F8)));
        pausePlayButton_->setToolTip(paused ? "Resume game" : "Pause game");
        pausePlayButton_->setAccessibleName(paused ? "Resume game" : "Pause game");
    }

    [[nodiscard]] qint64 wagerAmount() const {
        bool valid = false;
        const auto amount = amountEdit_->text().toLongLong(&valid);
        return valid ? amount : 0;
    }

    void setWagerAmount(qint64 amount) {
        const auto normalized = normalizedWagerAmount(amount);
        amountEdit_->setText(QString::number(normalized));
        updateWagerSummary(wagerRaiseAvailable_, wagerCurrentBet_);
    }

    void updateWagerSummary(bool raiseAvailable, qint64 currentBet) {
        wagerRaiseAvailable_ = raiseAvailable;
        if (!humanPlayer_) {
            totalCommitment_->setText("—");
            raiseSummary_->clear();
            return;
        }
        const auto betAmount = amountEdit_->isEnabled() ? wagerAmount() : 0;
        const auto totalCommitment = wagerExistingCommitment_ + betAmount;
        totalCommitment_->setText(QLocale().toString(totalCommitment));
        if (raiseAvailable) {
            const auto raiseBy = std::max<qint64>(0, totalCommitment - currentBet);
            raiseSummary_->setText("Raise by " + QLocale().toString(raiseBy) + " / Raise to " + QLocale().toString(totalCommitment));
        } else {
            raiseSummary_->clear();
        }
    }

    void normalizeWagerAmount() {
        if (!amountEdit_->isEnabled()) return;
        setWagerAmount(wagerAmount());
    }

    void adjustWagerAmount(qint64 adjustment) {
        if (!amountEdit_->isEnabled()) return;
        setWagerAmount(wagerAmount() + adjustment);
    }

    void setChipDenominations(const QJsonArray& values) {
        QVector<qint64> denominations;
        for (const auto& value : values) {
            const auto denomination = value.toInteger();
            if (denomination > 0 && !denominations.contains(denomination)) denominations.append(denomination);
        }
        std::sort(denominations.begin(), denominations.end());
        if (denominations == chipDenominations_) return;
        chipDenominations_ = std::move(denominations);
        while (auto* item = denominationLayout_->takeAt(0)) {
            if (auto* widget = item->widget()) widget->deleteLater();
            delete item;
        }
        denominationUp_.clear();
        denominationDown_.clear();
        for (const auto denomination : chipDenominations_) {
            auto* controls = new QWidget(denominationControls_);
            auto* controlsLayout = new QVBoxLayout(controls);
            controlsLayout->setContentsMargins(0, 0, 0, 0);
            auto* up = new QToolButton(controls);
            up->setText("▲");
            up->setEnabled(false);
            auto* down = new QToolButton(controls);
            down->setText("▼");
            down->setEnabled(false);
            connect(up, &QToolButton::clicked, this, [this, denomination] { adjustWagerAmount(denomination); });
            connect(down, &QToolButton::clicked, this, [this, denomination] { adjustWagerAmount(-denomination); });
            controlsLayout->addWidget(up, 0, Qt::AlignHCenter);
            controlsLayout->addWidget(new QLabel(QLocale().toString(denomination), controls), 0, Qt::AlignHCenter);
            controlsLayout->addWidget(down, 0, Qt::AlignHCenter);
            denominationLayout_->addWidget(controls);
            denominationUp_.append(up);
            denominationDown_.append(down);
        }
    }

    [[nodiscard]] qint64 normalizedWagerAmount(qint64 amount) const {
        if (wagerMaximum_ < wagerMinimum_) return 0;
        const auto bounded = std::max(amount, wagerMinimum_);
        if (chipDenominations_.isEmpty()) return std::clamp(bounded, wagerMinimum_, wagerMaximum_);
        const auto unit = chipDenominations_.front();
        const auto roundedUp = ((bounded + unit - 1) / unit) * unit;
        const auto maximumChipValue = (wagerMaximum_ / unit) * unit;
        return std::clamp(std::min(roundedUp, maximumChipValue), wagerMinimum_, maximumChipValue);
    }

    void beginNextDealCountdown(int delayMilliseconds) {
        if (pauseReason_ == PauseReason::deal) { paused_ = false; pauseReason_ = PauseReason::none; }
        nextDealMilliseconds_ = delayMilliseconds;
        nextDealIn_->setText(countdownValue(nextDealMilliseconds_));
        dealNowButton_->setEnabled(true);
        pausePlayButton_->setEnabled(true);
        setPausePlayButtonMode(false);
        nextDealCountdownTimer_.start(50);
        nextHandTimer_.start(nextDealMilliseconds_);
    }

    void stopNextDealCountdown() {
        nextHandTimer_.stop();
        nextDealCountdownTimer_.stop();
        nextDealMilliseconds_ = 0;
        if (nextDealIn_) nextDealIn_->setText("—");
        if (dealNowButton_) dealNowButton_->setEnabled(false);
        if (pauseReason_ == PauseReason::deal) { paused_ = false; pauseReason_ = PauseReason::none; }
        if (turnSequence_ < 0) pausePlayButton_->setEnabled(false);
    }

    void advanceNextDealCountdown() {
        if (nextDealMilliseconds_ <= 0) {
            nextDealCountdownTimer_.stop();
            return;
        }
        nextDealMilliseconds_ = std::max(0, nextDealMilliseconds_ - 50);
        nextDealIn_->setText(countdownValue(nextDealMilliseconds_));
    }

    void beginTurnCountdown(const QJsonObject& legal) {
        const auto canCheck = legal.value("check").toBool();
        const auto canFold = legal.value("fold").toBool();
        if (turnSequence_ == tableSequence_) {
            turnCanCheck_ = canCheck;
            turnCanFold_ = canFold;
            setActionClockVisible(true);
            return;
        }
        turnSequence_ = tableSequence_;
        turnSeconds_ = settings_.playerClockSeconds;
        turnCanCheck_ = canCheck;
        turnCanFold_ = canFold;
        turnCountdownTimer_.start(1'000);
        setActionClockVisible(true);
        pausePlayButton_->setEnabled(true);
        setPausePlayButtonMode(false);
    }

    void stopTurnCountdown() {
        turnCountdownTimer_.stop();
        turnSequence_ = -1;
        turnSeconds_ = 0;
        turnCanCheck_ = false;
        turnCanFold_ = false;
        setActionClockVisible(false);
        setActionDetailsVisible(false);
        if (pauseReason_ == PauseReason::turn) { paused_ = false; pauseReason_ = PauseReason::none; }
        if (nextDealMilliseconds_ == 0) pausePlayButton_->setEnabled(false);
    }

    void togglePause() {
        if (!paused_) {
            if (turnCountdownTimer_.isActive()) {
                turnCountdownTimer_.stop();
                pauseReason_ = PauseReason::turn;
                setActionClockVisible(true);
            } else if (nextHandTimer_.isActive()) {
                nextHandTimer_.stop();
                nextDealCountdownTimer_.stop();
                pauseReason_ = PauseReason::deal;
                statusBar()->showMessage("Game paused. Next deal in " + durationText(nextDealMilliseconds_) + ".");
            } else {
                return;
            }
            paused_ = true;
            setPausePlayButtonMode(true);
            return;
        }
        paused_ = false;
        if (pauseReason_ == PauseReason::turn) {
            turnCountdownTimer_.start(1'000);
            statusBar()->clearMessage();
            setActionClockVisible(true);
        } else if (pauseReason_ == PauseReason::deal) {
            nextDealCountdownTimer_.start(50);
            nextHandTimer_.start(nextDealMilliseconds_);
        }
        pauseReason_ = PauseReason::none;
        setPausePlayButtonMode(false);
    }

    void advanceTurnCountdown() {
        if (turnSequence_ != tableSequence_ || !humanPlayer_) {
            stopTurnCountdown();
            return;
        }
        if (turnSeconds_ > 0) --turnSeconds_;
        if (turnSeconds_ > 0) {
            setActionClockVisible(true);
            return;
        }
        turnCountdownTimer_.stop();
        if (turnCanCheck_) selectHumanAction("Check");
        else if (turnCanFold_) selectHumanAction("Fold");
        else refreshTableView();
    }

    void startNextHand() {
        if (!connected_ || competition_->currentIndex() < 0 || table_->currentIndex() < 0) return;
        if (nextHandRequestInFlight_) return;
        nextHandRequestInFlight_ = true;
        stopNextDealCountdown();
        const auto attempt = connectionGeneration_;
        const auto path = "/v1/competitions/" + competition_->currentText() + "/tables/" + table_->currentText() + "/next-hand";
        auto* reply = postJson(path, QJsonObject{{"viewer", humanPlayer_ ? humanPlayer_->apiPlayerName() : QString{}}});
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
            nextHandRequestInFlight_ = false;
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                statusBar()->showMessage(response.value("error").toString("The server could not begin the next hand."));
                refreshTableView();
                return;
            }
            applyTableView(response);
            statusBar()->showMessage("The server started the next hand.");
        });
    }

    void restartGame() {
        if (!connected_ || competition_->currentIndex() < 0 || table_->currentIndex() < 0) return;
        if (!tableComplete_ && lastStreet_ != "Showdown") {
            QMessageBox warning(this);
            warning.setIcon(QMessageBox::Warning);
            warning.setWindowTitle("Restart active game?");
            warning.setText("The current hand is not finished. Restarting resets every stack and begins a new hand on this table.");
            auto* restart = warning.addButton("Restart Game", QMessageBox::DestructiveRole);
            warning.addButton(QMessageBox::Cancel);
            warning.exec();
            if (warning.clickedButton() != restart) return;
        }
        stopNextDealCountdown();
        stopTurnCountdown();
        displayedActionHistoryCount_ = 0;
        displayedActionHistory_.clear();
        presentedCommunityCards_.clear();
        queuedCommunityCardCount_ = 0;
        automatedActionQueue_.clear();
        automatedActionTimer_.stop();
        pokerTable_->clearActionBoxes();
        const auto attempt = connectionGeneration_;
        const auto path = "/v1/competitions/" + competition_->currentText() + "/tables/" + table_->currentText() + "/restart";
        auto* reply = postJson(path, QJsonObject{{"viewer", humanPlayer_ ? humanPlayer_->apiPlayerName() : QString{}}});
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 200;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "Restart failed", response.value("error").toString("The server could not restart this table."));
                refreshTableView();
                return;
            }
            applyTableView(response, true);
            statusBar()->showMessage("The server restarted the current table.");
        });
    }

    void newGame() {
        if (!connected_) return;
        bool accepted = false;
        const auto playerName = QInputDialog::getText(this, "Your player", "Player name:", QLineEdit::Normal, settings_.defaultPlayerName, &accepted).trimmed();
        if (!accepted) return;
        if (playerName.isEmpty()) {
            QMessageBox::warning(this, "Player name required", "Choose a name using letters, digits, and dashes.");
            return;
        }
        settings_.defaultPlayerName = playerName;
        bluffskill::app_config::AppConfig::save(settings_);
        const auto attempt = connectionGeneration_;
        pendingHumanPlayer_ = std::make_unique<bluffskill::client::HumanPlayer>(playerName);
        setNewGameActionsEnabled(false);
        const auto humanSeat = QRandomGenerator::global()->bounded(1, defaultTableSeats + 1);
        const auto referenceTypes = randomReferencePlayerTypes(defaultTableSeats - 1);
        QVector<QString> referencesBeforeHuman;
        QVector<QString> referencesAfterHuman;
        for (qsizetype index = 0; index < referenceTypes.size(); ++index) {
            (index < humanSeat - 1 ? referencesBeforeHuman : referencesAfterHuman).append(referenceTypes[index]);
        }
        statusBar()->showMessage("Creating a nine-reference-player tournament…");
        auto* reply = postJson("/v1/competitions", QJsonObject{
            {"flavor", "NoLimitTexasHoldEm"},
            {"maximumPlayers", defaultTableSeats},
            {"startingStack", 7000},
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, humanSeat,
            referencesBeforeHuman = std::move(referencesBeforeHuman), referencesAfterHuman = std::move(referencesAfterHuman)]() mutable {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto competition = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "New game failed", "The server could not create a new competition.");
                pendingHumanPlayer_.reset();
                setNewGameActionsEnabled(true);
                return;
            }
            const auto competitionName = competition.value("name").toString();
            addReferencePlayers(competitionName, attempt, std::move(referencesBeforeHuman),
                [this, competitionName, attempt, humanSeat, referencesAfterHuman = std::move(referencesAfterHuman)]() mutable {
                    attachHumanPlayer(competitionName, "Red", attempt, humanSeat, std::move(referencesAfterHuman));
                });
        });
    }

    void customGame() {
        if (!connected_) return;
        CustomGameDialog dialog(settings_.defaultPlayerName, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const auto configuration = dialog.configuration();
        if (configuration.includeHuman) {
            settings_.defaultPlayerName = configuration.playerName;
            bluffskill::app_config::AppConfig::save(settings_);
            pendingHumanPlayer_ = std::make_unique<bluffskill::client::HumanPlayer>(configuration.playerName);
        } else {
            clearHumanPlayer();
        }

        auto referenceTypes = referencePlayerTypes(configuration.leoPlayers, configuration.virgoPlayers);
        const auto humanSeat = configuration.includeHuman ? QRandomGenerator::global()->bounded(1, defaultTableSeats + 1) : 0;
        QVector<QString> referencesBeforeHuman;
        QVector<QString> referencesAfterHuman;
        for (qsizetype index = 0; index < referenceTypes.size(); ++index) {
            (configuration.includeHuman && index >= humanSeat - 1 ? referencesAfterHuman : referencesBeforeHuman).append(referenceTypes[index]);
        }

        const auto attempt = connectionGeneration_;
        setNewGameActionsEnabled(false);
        statusBar()->showMessage("Creating a custom tournament…");
        auto* reply = postJson("/v1/competitions", QJsonObject{
            {"flavor", "NoLimitTexasHoldEm"},
            {"maximumPlayers", defaultTableSeats},
            {"startingStack", 7000},
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, configuration, humanSeat,
            referencesBeforeHuman = std::move(referencesBeforeHuman), referencesAfterHuman = std::move(referencesAfterHuman)]() mutable {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto competition = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "Custom game failed", "The server could not create a new competition.");
                pendingHumanPlayer_.reset();
                setNewGameActionsEnabled(true);
                return;
            }
            const auto competitionName = competition.value("name").toString();
            addReferencePlayers(competitionName, attempt, std::move(referencesBeforeHuman),
                [this, competitionName, attempt, configuration, humanSeat, referencesAfterHuman = std::move(referencesAfterHuman)]() mutable {
                    if (configuration.includeHuman) {
                        attachHumanPlayer(competitionName, "Red", attempt, humanSeat, std::move(referencesAfterHuman));
                        return;
                    }
                    setNewGameActionsEnabled(true);
                    statusBar()->showMessage("Created " + competitionName + " with "
                        + QString::number(configuration.leoPlayers) + " Leo and "
                        + QString::number(configuration.virgoPlayers) + " Virgo reference players.");
                    refreshCompetitions(competitionName);
                });
        });
    }

    [[nodiscard]] static QVector<QString> randomReferencePlayerTypes(int count) {
        QVector<QString> types;
        types.reserve(count);
        for (int index = 0; index < count; ++index) {
            types.append(QRandomGenerator::global()->bounded(2) == 0 ? "Leo" : "Virgo");
        }
        if (count > 1) {
            const auto hasLeo = std::ranges::any_of(types, [](const QString& type) { return type == "Leo"; });
            const auto hasVirgo = std::ranges::any_of(types, [](const QString& type) { return type == "Virgo"; });
            if (!hasLeo) types[QRandomGenerator::global()->bounded(count)] = "Leo";
            if (!hasVirgo) types[QRandomGenerator::global()->bounded(count)] = "Virgo";
        }
        return types;
    }

    [[nodiscard]] static QVector<QString> referencePlayerTypes(int leoPlayers, int virgoPlayers) {
        QVector<QString> types;
        types.reserve(leoPlayers + virgoPlayers);
        for (int index = 0; index < leoPlayers; ++index) types.append("Leo");
        for (int index = 0; index < virgoPlayers; ++index) types.append("Virgo");
        for (int index = types.size() - 1; index > 0; --index) {
            std::swap(types[index], types[QRandomGenerator::global()->bounded(index + 1)]);
        }
        return types;
    }

    void addReferencePlayers(const QString& competitionName, std::uint64_t attempt, QVector<QString> types, std::function<void()> onSuccess) {
        if (types.isEmpty()) {
            onSuccess();
            return;
        }
        const auto type = types.takeFirst();
        const auto path = "/v1/competitions/" + competitionName + "/reference-players";
        auto* reply = postJson(path, QJsonObject{{"count", 1}, {"type", type}});
        connect(reply, &QNetworkReply::finished, this, [this, reply, competitionName, attempt,
            types = std::move(types), onSuccess = std::move(onSuccess)]() mutable {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                QMessageBox::warning(this, "Reference players failed", "The competition was created, but its reference players were not added.");
                pendingHumanPlayer_.reset();
                setNewGameActionsEnabled(true);
                refreshCompetitions(competitionName);
                return;
            }
            addReferencePlayers(competitionName, attempt, std::move(types), std::move(onSuccess));
        });
    }

    void attachHumanPlayer(const QString& competitionName, const QString& tableName, std::uint64_t attempt, int humanSeat,
        QVector<QString> referencesAfterHuman) {
        if (!pendingHumanPlayer_) return;
        auto* reply = track(pendingHumanPlayer_->attachToTable(network_, serverUrl_, competitionName, tableName));
        connect(reply, &QNetworkReply::finished, this, [this, reply, competitionName, attempt, humanSeat,
            referencesAfterHuman = std::move(referencesAfterHuman)]() mutable {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = QJsonDocument::fromJson(reply->readAll()).object();
            const auto success = reply->error() == QNetworkReply::NoError && status == 201;
            release(reply);
            if (attempt != connectionGeneration_ || !connected_) return;
            if (!success) {
                const auto detail = response.value("error").toString("The server could not attach your player.");
                QMessageBox::warning(this, "Player attachment failed", detail);
                pendingHumanPlayer_.reset();
                setNewGameActionsEnabled(true);
                refreshCompetitions(competitionName);
                return;
            }
            humanPlayer_ = std::move(pendingHumanPlayer_);
            pokerTable_->setLocalPlayerName(humanPlayer_->apiPlayerName());
            setActionControlsEnabled(false);
            statusBar()->showMessage("You are " + humanPlayer_->apiPlayerName() + ". Seating remaining reference players…");
            addReferencePlayers(competitionName, attempt, std::move(referencesAfterHuman), [this, competitionName] {
                setNewGameActionsEnabled(true);
                statusBar()->showMessage("You are " + humanPlayer_->apiPlayerName() + ". Retrieving your private table view…");
                statusBar()->showMessage("Created " + competitionName + " with nine reference players and " + humanPlayer_->apiPlayerName() + ".");
                refreshCompetitions(competitionName);
            });
        });
    }

    void selectHumanAction(const char* actionName) {
        if (!humanPlayer_ || automatedActionTimer_.isActive() || !automatedActionQueue_.isEmpty()) return;
        stopTurnCountdown();
        const auto action = QString::fromUtf8(actionName);
        const auto humanAction = action == "Check" ? bluffskill::client::HumanAction::check
            : action == "Call" ? bluffskill::client::HumanAction::call
            : action == "Bet" ? bluffskill::client::HumanAction::bet
            : action == "Raise" ? bluffskill::client::HumanAction::raise
            : bluffskill::client::HumanAction::fold;
        humanPlayer_->selectAction(humanAction);
        if (competition_->currentIndex() < 0 || table_->currentIndex() < 0) return;
        // If this command completes the hand, preserve its action history so
        // the local action is shown before the queued reference-player actions.
        presentFinalAutomatedActions_ = true;
        const auto amount = humanAction == bluffskill::client::HumanAction::bet || humanAction == bluffskill::client::HumanAction::raise
            ? wagerExistingCommitment_ + wagerAmount() : 0;
        setActionControlsEnabled(false);
        setWagerControlsEnabled(false);
        statusBar()->showMessage("Submitting " + bluffskill::client::HumanPlayer::displayName(humanAction) + "…");
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
                presentFinalAutomatedActions_ = false;
                const auto detail = response.value("error").toString("The server rejected the action.");
                statusBar()->showMessage(detail);
                statusBar()->showMessage("Action was not accepted; refreshing the table.");
                refreshTableView();
                return;
            }
            applyTableView(response);
            if (!automatedActionTimer_.isActive()) statusBar()->showMessage("Action accepted by the server.");
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
        localHoleCardsWidget_->setCards(QStringList{});
        setActionControlsEnabled(false);
        setWagerControlsEnabled(false);
        amountEdit_->clear();
        tableSequence_ = 0;
        lastShowdownSequence_ = -1;
        displayedActionHistoryCount_ = 0;
        displayedActionHistory_.clear();
        presentedCommunityCards_.clear();
        queuedCommunityCardCount_ = 0;
        automatedActionQueue_.clear();
        automatedActionTimer_.stop();
        presentFinalAutomatedActions_ = false;
        pokerTable_->clearActionBoxes();
        tableComplete_ = false;
        remainingPlayersCaption_->setText("Remaining Players:");
        remainingPlayers_->setText("—");
        nextHandRequestInFlight_ = false;
        stopNextDealCountdown();
        stopTurnCountdown();
        statusBar()->showMessage("No human player is attached.");
        totalCommitment_->setText("—");
        raiseSummary_->clear();
        smallBlindAmount_->setText("—");
        bigBlindAmount_->setText("—");
        roundsPlayed_->setText("—");
    }

private:
    enum class PauseReason { none, turn, deal };

    struct AutomatedActionPresentation {
        enum class Type { playerAction, dealerReveal };

        Type type{Type::playerAction};
        std::size_t seat{0};
        QString player;
        QString action;
        qint64 amount{0};
        qint64 stackAfter{0};
        qint64 pot{0};
        qint64 currentBet{0};
        QStringList communityCards;
    };

    bluffskill::app_config::Settings settings_;
    QNetworkAccessManager network_{this};
    QSet<QNetworkReply*> activeReplies_;
    QNetworkReply* healthReply_{};
    QUrl serverUrl_;
    std::uint64_t connectionGeneration_{};
    std::uint64_t tableRequestGeneration_{};
    std::uint64_t tableViewRequestGeneration_{};
    qint64 tableSequence_{};
    qint64 lastShowdownSequence_{-1};
    qint64 turnSequence_{-1};
    QString lastStreet_;
    qsizetype displayedActionHistoryCount_{};
    QStringList displayedActionHistory_;
    QStringList presentedCommunityCards_;
    qsizetype queuedCommunityCardCount_{};
    int nextDealMilliseconds_{};
    int turnSeconds_{};
    bool connected_{};
    bool paused_{};
    bool nextHandRequestInFlight_{};
    bool autoConnectInProgress_{};
    bool tableComplete_{};
    bool turnCanCheck_{};
    bool turnCanFold_{};
    bool presentFinalAutomatedActions_{};
    PauseReason pauseReason_{PauseReason::none};
    QTimer nextHandTimer_;
    QTimer nextDealCountdownTimer_;
    QTimer turnCountdownTimer_;
    QTimer automatedActionTimer_;
    QVector<AutomatedActionPresentation> automatedActionQueue_;
    QComboBox* server_{};
    QComboBox* competition_{};
    QComboBox* table_{};
    QLineEdit* remainingPlayers_{};
    QLabel* remainingPlayersCaption_{};
    QLineEdit* smallBlindAmount_{};
    QLineEdit* bigBlindAmount_{};
    QLineEdit* roundsPlayed_{};
    QLineEdit* nextDealIn_{};
    QPushButton* dealNowButton_{};
    PokerTable* pokerTable_{};
    LocalHoleCardsWidget* localHoleCardsWidget_{};
    QLabel* actionClockCaption_{};
    QLineEdit* actionClockValue_{};
    QLabel* callAmountCaption_{};
    QLineEdit* callAmountValue_{};
    QLabel* minimumWagerCaption_{};
    QLineEdit* minimumWagerValue_{};
    QPushButton* pausePlayButton_{};
    QLineEdit* amountEdit_{};
    QLineEdit* totalCommitment_{};
    QLabel* raiseSummary_{};
    QWidget* denominationControls_{};
    QHBoxLayout* denominationLayout_{};
    QVector<QToolButton*> denominationUp_;
    QVector<QToolButton*> denominationDown_;
    QVector<qint64> chipDenominations_;
    qint64 wagerMinimum_{};
    qint64 wagerMaximum_{};
    qint64 wagerExistingCommitment_{};
    qint64 wagerCurrentBet_{};
    bool wagerRaiseAvailable_{};
    std::vector<QPushButton*> actionButtons_;
    QAction* disconnectAction_{};
    QAction* newGameAction_{};
    QAction* customGameAction_{};
    QAction* restartGameAction_{};
    qsizetype autoConnectPortIndex_{};
    std::unique_ptr<bluffskill::client::HumanPlayer> pendingHumanPlayer_;
    std::unique_ptr<bluffskill::client::HumanPlayer> humanPlayer_;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("AzoneLayer");
    QCoreApplication::setOrganizationDomain("azonelayer.com");
    ClientWindow window;
    window.show();
    return application.exec();
}
