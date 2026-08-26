#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QIntValidator>
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
#include <QUrl>
#include <QVBoxLayout>
#include <QLineEdit>

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
        QUrl url;
        url.setScheme("http"); // The prototype server is intentionally localhost HTTP only.
        url.setHost(host_->text().trimmed());
        url.setPort(port_->text().toInt());
        url.setPath("/v1/health");
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
        layout->addWidget(new PokerTable(central), 1);
        auto* actions = new QFrame(central);
        auto* actionLayout = new QHBoxLayout(actions);
        actionLayout->addWidget(new QLabel("Amount: 0", actions));
        for (const auto* action : {"Check", "Call", "Bet", "Fold"}) {
            auto* button = new QPushButton(action, actions); button->setEnabled(false); actionLayout->addWidget(button);
        }
        layout->addWidget(actions);
        setCentralWidget(central);
        auto* connectAction = menuBar()->addMenu("Connection")->addAction("Connect…");
        connect(connectAction, &QAction::triggered, this, [this] { connectToServer(); });
        statusBar()->showMessage("Choose Connection → Connect… to begin.");
    }

private:
    void connectToServer() {
        ConnectionDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;

        const auto address = dialog.displayAddress();
        statusBar()->showMessage("Connecting to " + address + "…");
        auto* reply = network_.get(QNetworkRequest(dialog.healthUrl()));
        connect(reply, &QNetworkReply::finished, this, [this, reply, address] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto response = reply->readAll();
            const auto body = QJsonDocument::fromJson(response).object();
            const auto connected = reply->error() == QNetworkReply::NoError && status == 200
                && body.value("status") == "ok";
            const auto error = reply->errorString();
            reply->deleteLater();

            if (!connected) {
                statusBar()->showMessage("Could not connect to " + address);
                const auto detail = status > 0 ? "The server returned HTTP " + QString::number(status) + "." : error;
                QMessageBox::warning(this, "Server unavailable",
                    "BluffSkill could not verify the server at " + address + ".\n\n" + detail);
                return;
            }

            server_->setEnabled(true);
            server_->clear();
            server_->addItem(address);
            competition_->clear();
            competition_->addItem("Competition list coming next");
            competition_->setEnabled(false);
            table_->clear();
            table_->addItem("Choose a competition first");
            table_->setEnabled(false);
            statusBar()->showMessage("Connected to " + address);
        });
    }

private:
    QNetworkAccessManager network_{this};
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
