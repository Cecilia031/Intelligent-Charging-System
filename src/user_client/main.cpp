#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <functional>

#include "ui_user_client.h"

namespace {

QString moneyText(int cents)
{
    return QStringLiteral("%1").arg(cents / 100.0, 0, 'f', 2);
}

int tableCurrentId(QTableWidget *table)
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    if (selected.isEmpty()) {
        return 0;
    }
    return table->item(selected.first()->row(), 0)->data(Qt::UserRole).toInt();
}

void setItem(QTableWidget *table, int row, int column, const QString &text, int id = 0)
{
    auto *item = new QTableWidgetItem(text);
    if (id > 0) {
        item->setData(Qt::UserRole, id);
    }
    table->setItem(row, column, item);
}

} // namespace

class UserWindow final : public QWidget {
    Q_OBJECT

public:
    explicit UserWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        buildUi();
        connectSignals();
    }

    ~UserWindow() override
    {
        delete ui;
    }

private:
    void buildUi()
    {
        ui = new Ui::UserWindow;
        ui->setupUi(this);

        setWindowTitle(QStringLiteral("Charging User Client"));
        resize(1180, 760);

        hostEdit_ = ui->hostEdit;
        portSpin_ = ui->portSpin;
        phoneEdit_ = ui->phoneEdit;
        nicknameEdit_ = ui->nicknameEdit;
        loginButton_ = ui->loginButton;
        updateProfileButton_ = ui->updateProfileButton;
        profileLabel_ = ui->profileLabel;

        keywordEdit_ = ui->keywordEdit;
        refreshStationsButton_ = ui->refreshStationsButton;
        stationTable_ = ui->stationTable;

        refreshChargersButton_ = ui->refreshChargersButton;
        chargerTable_ = ui->chargerTable;

        createOrderButton_ = ui->createOrderButton;
        cancelOrderButton_ = ui->cancelOrderButton;
        startOrderButton_ = ui->startOrderButton;
        stopOrderButton_ = ui->stopOrderButton;
        settleOrderButton_ = ui->settleOrderButton;
        refreshCurrentButton_ = ui->refreshCurrentButton;
        refreshOrdersButton_ = ui->refreshOrdersButton;
        rechargeButton_ = ui->rechargeButton;
        energySpin_ = ui->energySpin;
        rechargeSpin_ = ui->rechargeSpin;
        currentOrderLabel_ = ui->currentOrderLabel;
        orderTable_ = ui->orderTable;
        balanceLogTable_ = ui->balanceLogTable;
        telemetryTable_ = ui->telemetryTable;
        logEdit_ = ui->logEdit;

        stationTable_->horizontalHeader()->setStretchLastSection(true);
        stationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        stationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        chargerTable_->horizontalHeader()->setStretchLastSection(true);
        chargerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        chargerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        orderTable_->horizontalHeader()->setStretchLastSection(true);
        orderTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        orderTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        balanceLogTable_->horizontalHeader()->setStretchLastSection(true);
        balanceLogTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        telemetryTable_->horizontalHeader()->setStretchLastSection(true);
        telemetryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        logEdit_->setReadOnly(true);
        logEdit_->setMaximumBlockCount(300);
    }

    void connectSignals()
    {
        connect(loginButton_, &QPushButton::clicked, this, &UserWindow::login);
        connect(updateProfileButton_, &QPushButton::clicked, this, &UserWindow::updateProfile);
        connect(refreshStationsButton_, &QPushButton::clicked, this, &UserWindow::refreshStations);
        connect(refreshChargersButton_, &QPushButton::clicked, this, &UserWindow::refreshChargers);
        connect(stationTable_, &QTableWidget::itemSelectionChanged, this, &UserWindow::refreshChargers);
        connect(chargerTable_, &QTableWidget::itemSelectionChanged, this, &UserWindow::refreshTelemetry);
        connect(createOrderButton_, &QPushButton::clicked, this, &UserWindow::createOrder);
        connect(cancelOrderButton_, &QPushButton::clicked, this, &UserWindow::cancelOrder);
        connect(startOrderButton_, &QPushButton::clicked, this, &UserWindow::startOrder);
        connect(stopOrderButton_, &QPushButton::clicked, this, &UserWindow::stopOrder);
        connect(settleOrderButton_, &QPushButton::clicked, this, &UserWindow::settleOrder);
        connect(refreshCurrentButton_, &QPushButton::clicked, this, &UserWindow::refreshCurrentOrder);
        connect(refreshOrdersButton_, &QPushButton::clicked, this, &UserWindow::refreshOrders);
        connect(rechargeButton_, &QPushButton::clicked, this, &UserWindow::recharge);
    }

    QJsonObject request(QJsonObject payload)
    {
        QTcpSocket socket;
        socket.connectToHost(hostEdit_->text().trimmed(), static_cast<quint16>(portSpin_->value()));
        if (!socket.waitForConnected(3000)) {
            throw QStringLiteral("connect failed: %1").arg(socket.errorString());
        }

        payload.insert(QStringLiteral("request_id"), ++requestId_);
        socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
        if (!socket.waitForBytesWritten(3000)) {
            throw QStringLiteral("write failed: %1").arg(socket.errorString());
        }
        if (!socket.waitForReadyRead(5000)) {
            throw QStringLiteral("read timeout: %1").arg(socket.errorString());
        }

        const QByteArray line = socket.readLine();
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            throw QStringLiteral("invalid response: %1").arg(QString::fromUtf8(line));
        }

        const QJsonObject response = doc.object();
        appendLog(QStringLiteral(">> %1").arg(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))));
        appendLog(QStringLiteral("<< %1").arg(QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact))));
        if (!response.value(QStringLiteral("success")).toBool()) {
            throw response.value(QStringLiteral("error")).toString(QStringLiteral("request failed"));
        }
        return response.value(QStringLiteral("data")).toObject();
    }

    QJsonObject authedPayload(const QString &action) const
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("action"), action);
        payload.insert(QStringLiteral("session_token"), sessionToken_);
        return payload;
    }

    void runAction(const QString &failureTitle, const std::function<void()> &action)
    {
        try {
            action();
        } catch (const QString &message) {
            QMessageBox::warning(this, failureTitle, message);
            appendLog(QStringLiteral("[ERROR] %1").arg(message));
        }
    }

    void login()
    {
        runAction(QStringLiteral("Login Failed"), [this]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("user.login"));
            payload.insert(QStringLiteral("phone"), phoneEdit_->text().trimmed());
            const QJsonObject data = request(payload);
            const QJsonObject user = data.value(QStringLiteral("user")).toObject();
            const QJsonObject session = data.value(QStringLiteral("session")).toObject();

            userId_ = user.value(QStringLiteral("id")).toInt();
            sessionToken_ = session.value(QStringLiteral("token")).toString();
            currentOrderId_ = 0;
            nicknameEdit_->setText(user.value(QStringLiteral("nickname")).toString());
            profileLabel_->setText(QStringLiteral("%1  balance: %2 yuan")
                                       .arg(user.value(QStringLiteral("phone")).toString())
                                       .arg(moneyText(user.value(QStringLiteral("balance_cents")).toInt())));
            refreshStations();
            refreshCurrentOrder();
            refreshOrders();
            refreshBalanceLogs();
        });
    }

    void updateProfile()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Update Failed"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("user.update_profile"));
            payload.insert(QStringLiteral("nickname"), nicknameEdit_->text().trimmed());
            const QJsonObject user = request(payload).value(QStringLiteral("user")).toObject();
            profileLabel_->setText(QStringLiteral("%1  balance: %2 yuan")
                                       .arg(user.value(QStringLiteral("phone")).toString())
                                       .arg(moneyText(user.value(QStringLiteral("balance_cents")).toInt())));
        });
    }

    void refreshStations()
    {
        runAction(QStringLiteral("Station Query Failed"), [this]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("station.list"));
            const QString keyword = keywordEdit_->text().trimmed();
            if (!keyword.isEmpty()) {
                payload.insert(QStringLiteral("keyword"), keyword);
            }
            const QJsonArray stations = request(payload).value(QStringLiteral("stations")).toArray();
            stationTable_->setRowCount(stations.size());
            for (int row = 0; row < stations.size(); ++row) {
                const QJsonObject station = stations.at(row).toObject();
                const int id = station.value(QStringLiteral("id")).toInt();
                setItem(stationTable_, row, 0, QString::number(id), id);
                setItem(stationTable_, row, 1, station.value(QStringLiteral("name")).toString(), id);
                setItem(stationTable_, row, 2, station.value(QStringLiteral("address")).toString(), id);
                setItem(stationTable_, row, 3, station.value(QStringLiteral("status")).toString(), id);
            }
            stationTable_->resizeColumnsToContents();
        });
    }

    void refreshChargers()
    {
        const int stationId = tableCurrentId(stationTable_);
        if (stationId <= 0) {
            return;
        }
        runAction(QStringLiteral("Charger Query Failed"), [this, stationId]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("charger.list"));
            payload.insert(QStringLiteral("station_id"), stationId);
            const QJsonArray chargers = request(payload).value(QStringLiteral("chargers")).toArray();
            chargerTable_->setRowCount(chargers.size());
            for (int row = 0; row < chargers.size(); ++row) {
                const QJsonObject charger = chargers.at(row).toObject();
                const int id = charger.value(QStringLiteral("id")).toInt();
                setItem(chargerTable_, row, 0, QString::number(id), id);
                setItem(chargerTable_, row, 1, charger.value(QStringLiteral("code")).toString(), id);
                setItem(chargerTable_, row, 2, charger.value(QStringLiteral("type")).toString(), id);
                setItem(chargerTable_, row, 3, QString::number(charger.value(QStringLiteral("power_kw")).toDouble()), id);
                setItem(chargerTable_, row, 4, charger.value(QStringLiteral("status")).toString(), id);
            }
            chargerTable_->resizeColumnsToContents();
        });
    }

    void createOrder()
    {
        if (!ensureLogin()) {
            return;
        }
        const int chargerId = tableCurrentId(chargerTable_);
        if (chargerId <= 0) {
            QMessageBox::information(this, QStringLiteral("Reserve"), QStringLiteral("Select a charger first."));
            return;
        }
        runAction(QStringLiteral("Reserve Failed"), [this, chargerId]() {
            QJsonObject payload = authedPayload(QStringLiteral("order.create"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            const QJsonObject order = request(payload).value(QStringLiteral("order")).toObject();
            currentOrderId_ = order.value(QStringLiteral("id")).toInt();
            showCurrentOrder(order);
            refreshOrders();
        });
    }

    void cancelOrder()
    {
        runOrderAction(QStringLiteral("order.cancel"), QStringLiteral("Cancel Failed"));
    }

    void startOrder()
    {
        runOrderAction(QStringLiteral("order.start"), QStringLiteral("Start Failed"));
    }

    void stopOrder()
    {
        if (!ensureLogin() || !ensureCurrentOrder()) {
            return;
        }
        runAction(QStringLiteral("Stop Failed"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("order.stop"));
            payload.insert(QStringLiteral("order_id"), currentOrderId_);
            payload.insert(QStringLiteral("energy_kwh"), energySpin_->value());
            const QJsonObject order = request(payload).value(QStringLiteral("order")).toObject();
            showCurrentOrder(order);
            refreshOrders();
            refreshTelemetry();
        });
    }

    void settleOrder()
    {
        runOrderAction(QStringLiteral("order.settle"), QStringLiteral("Settle Failed"));
        refreshProfile();
        refreshBalanceLogs();
    }

    void runOrderAction(const QString &actionName, const QString &failureTitle)
    {
        if (!ensureLogin() || !ensureCurrentOrder()) {
            return;
        }
        runAction(failureTitle, [this, actionName]() {
            QJsonObject payload = authedPayload(actionName);
            payload.insert(QStringLiteral("order_id"), currentOrderId_);
            const QJsonObject order = request(payload).value(QStringLiteral("order")).toObject();
            showCurrentOrder(order);
            refreshOrders();
            refreshChargers();
        });
    }

    void refreshCurrentOrder()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Current Order Failed"), [this]() {
            const QJsonObject data = request(authedPayload(QStringLiteral("order.current")));
            const QJsonValue value = data.value(QStringLiteral("order"));
            if (value.isNull() || !value.isObject()) {
                currentOrderId_ = 0;
                currentOrderLabel_->setText(QStringLiteral("No active order"));
                return;
            }
            const QJsonObject order = value.toObject();
            currentOrderId_ = order.value(QStringLiteral("id")).toInt();
            showCurrentOrder(order, data);
        });
    }

    void refreshOrders()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Order Query Failed"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("order.list"));
            payload.insert(QStringLiteral("limit"), 50);
            const QJsonArray orders = request(payload).value(QStringLiteral("orders")).toArray();
            orderTable_->setRowCount(orders.size());
            for (int row = 0; row < orders.size(); ++row) {
                const QJsonObject order = orders.at(row).toObject();
                const int id = order.value(QStringLiteral("id")).toInt();
                setItem(orderTable_, row, 0, QString::number(id), id);
                setItem(orderTable_, row, 1, order.value(QStringLiteral("order_no")).toString(), id);
                setItem(orderTable_, row, 2, order.value(QStringLiteral("charger_code")).toString(), id);
                setItem(orderTable_, row, 3, order.value(QStringLiteral("station_name")).toString(), id);
                setItem(orderTable_, row, 4, order.value(QStringLiteral("status")).toString(), id);
                setItem(orderTable_, row, 5, QString::number(order.value(QStringLiteral("energy_kwh")).toDouble(), 'f', 2), id);
                setItem(orderTable_, row, 6, moneyText(order.value(QStringLiteral("amount_cents")).toInt()), id);
            }
            orderTable_->resizeColumnsToContents();
        });
    }

    void recharge()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Recharge Failed"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("balance.recharge"));
            payload.insert(QStringLiteral("amount"), rechargeSpin_->value());
            request(payload);
            refreshProfile();
            refreshBalanceLogs();
        });
    }

    void refreshProfile()
    {
        if (!ensureLogin()) {
            return;
        }
        const QJsonObject user = request(authedPayload(QStringLiteral("user.profile"))).value(QStringLiteral("user")).toObject();
        profileLabel_->setText(QStringLiteral("%1  balance: %2 yuan")
                                   .arg(user.value(QStringLiteral("phone")).toString())
                                   .arg(moneyText(user.value(QStringLiteral("balance_cents")).toInt())));
    }

    void refreshBalanceLogs()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Balance Logs Failed"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("balance.logs"));
            payload.insert(QStringLiteral("limit"), 30);
            const QJsonArray logs = request(payload).value(QStringLiteral("logs")).toArray();
            balanceLogTable_->setRowCount(logs.size());
            for (int row = 0; row < logs.size(); ++row) {
                const QJsonObject log = logs.at(row).toObject();
                const int id = log.value(QStringLiteral("id")).toInt();
                setItem(balanceLogTable_, row, 0, QString::number(id), id);
                setItem(balanceLogTable_, row, 1, moneyText(log.value(QStringLiteral("change_cents")).toInt()), id);
                setItem(balanceLogTable_, row, 2, moneyText(log.value(QStringLiteral("balance_after_cents")).toInt()), id);
                setItem(balanceLogTable_, row, 3, log.value(QStringLiteral("reason")).toString(), id);
                setItem(balanceLogTable_, row, 4, log.value(QStringLiteral("created_at")).toString(), id);
            }
            balanceLogTable_->resizeColumnsToContents();
        });
    }

    void refreshTelemetry()
    {
        const int chargerId = tableCurrentId(chargerTable_);
        if (chargerId <= 0) {
            return;
        }
        runAction(QStringLiteral("Telemetry Failed"), [this, chargerId]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("telemetry.list"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            payload.insert(QStringLiteral("limit"), 20);
            const QJsonArray rows = request(payload).value(QStringLiteral("telemetry")).toArray();
            telemetryTable_->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                const QJsonObject record = rows.at(row).toObject();
                const int id = record.value(QStringLiteral("id")).toInt();
                setItem(telemetryTable_, row, 0, QString::number(id), id);
                setItem(telemetryTable_, row, 1, record.value(QStringLiteral("status")).toString(), id);
                setItem(telemetryTable_, row, 2, QString::number(record.value(QStringLiteral("power_kw")).toDouble(), 'f', 2), id);
                setItem(telemetryTable_, row, 3, QString::number(record.value(QStringLiteral("energy_kwh")).toDouble(), 'f', 2), id);
                setItem(telemetryTable_, row, 4, record.value(QStringLiteral("recorded_at")).toString(), id);
            }
            telemetryTable_->resizeColumnsToContents();
        });
    }

    void showCurrentOrder(const QJsonObject &order, const QJsonObject &extra = {})
    {
        currentOrderId_ = order.value(QStringLiteral("id")).toInt();
        QString text = QStringLiteral("Order #%1  %2  charger: %3  energy: %4 kWh  amount: %5 yuan")
                           .arg(currentOrderId_)
                           .arg(order.value(QStringLiteral("status")).toString())
                           .arg(order.value(QStringLiteral("charger_code")).toString())
                           .arg(order.value(QStringLiteral("energy_kwh")).toDouble(), 0, 'f', 2)
                           .arg(moneyText(order.value(QStringLiteral("amount_cents")).toInt()));
        if (!extra.isEmpty() && extra.contains(QStringLiteral("estimated_amount_cents"))) {
            text += QStringLiteral("  estimated: %1 yuan")
                        .arg(moneyText(extra.value(QStringLiteral("estimated_amount_cents")).toInt()));
        }
        currentOrderLabel_->setText(text);
    }

    bool ensureLogin()
    {
        if (sessionToken_.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Login Required"), QStringLiteral("Please login first."));
            return false;
        }
        return true;
    }

    bool ensureCurrentOrder()
    {
        if (currentOrderId_ <= 0) {
            QMessageBox::information(this, QStringLiteral("Order Required"), QStringLiteral("No active order. Reserve or refresh current order first."));
            return false;
        }
        return true;
    }

    void appendLog(const QString &line)
    {
        logEdit_->appendPlainText(QStringLiteral("[%1] %2")
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                      .arg(line.trimmed()));
    }

    QLineEdit *hostEdit_ = nullptr;
    QSpinBox *portSpin_ = nullptr;
    QLineEdit *phoneEdit_ = nullptr;
    QLineEdit *nicknameEdit_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QPushButton *updateProfileButton_ = nullptr;
    QLabel *profileLabel_ = nullptr;

    QLineEdit *keywordEdit_ = nullptr;
    QPushButton *refreshStationsButton_ = nullptr;
    QTableWidget *stationTable_ = nullptr;
    QPushButton *refreshChargersButton_ = nullptr;
    QTableWidget *chargerTable_ = nullptr;

    QPushButton *createOrderButton_ = nullptr;
    QPushButton *cancelOrderButton_ = nullptr;
    QPushButton *startOrderButton_ = nullptr;
    QPushButton *stopOrderButton_ = nullptr;
    QPushButton *settleOrderButton_ = nullptr;
    QPushButton *refreshCurrentButton_ = nullptr;
    QPushButton *refreshOrdersButton_ = nullptr;
    QPushButton *rechargeButton_ = nullptr;
    QDoubleSpinBox *energySpin_ = nullptr;
    QDoubleSpinBox *rechargeSpin_ = nullptr;
    QLabel *currentOrderLabel_ = nullptr;
    QTableWidget *orderTable_ = nullptr;
    QTableWidget *balanceLogTable_ = nullptr;
    QTableWidget *telemetryTable_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
    Ui::UserWindow *ui = nullptr;

    QString sessionToken_;
    int userId_ = 0;
    int currentOrderId_ = 0;
    int requestId_ = 0;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    UserWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
