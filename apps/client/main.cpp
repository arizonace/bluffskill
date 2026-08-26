#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {

class PokerTable final : public QWidget {
public:
    explicit PokerTable(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(900, 520); }

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
            painter.setPen(QPen(i == 5 ? QColor("#F6D365") : Qt::white, i == 5 ? 3 : 1));
            painter.drawEllipse(point, 43, 43);
            painter.drawText(QRectF(point.x() - 58, point.y() - 10, 116, 20), Qt::AlignCenter, "Seat " + QString::number(i + 1));
        }
    }
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
        layout->addWidget(new PokerTable(central), 1);
        auto* actions = new QFrame(central);
        auto* actionLayout = new QHBoxLayout(actions);
        actionLayout->addWidget(new QLabel("Amount: 0", actions));
        for (const auto* action : {"Check", "Call", "Bet", "Fold"}) {
            auto* button = new QPushButton(action, actions); button->setEnabled(false); actionLayout->addWidget(button);
        }
        layout->addWidget(actions);
        setCentralWidget(central);
        menuBar()->addMenu("Connection")->addAction("Connect…");
        statusBar()->showMessage("Choose Connection → Connect… to begin.");
    }
private:
    QComboBox* server_{};
    QComboBox* competition_{};
    QComboBox* table_{};
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    ClientWindow window;
    window.show();
    return application.exec();
}
