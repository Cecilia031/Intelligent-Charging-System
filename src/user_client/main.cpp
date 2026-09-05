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

QString statusText(const QString &value)
{
    if (value == QStringLiteral("active")) {
        return QStringLiteral("正常");
    }
    if (value == QStringLiteral("frozen")) {
        return QStringLiteral("冻结");
    }
    if (value == QStringLiteral("open")) {
        return QStringLiteral("开放");
    }
    if (value == QStringLiteral("closed")) {
        return QStringLiteral("关闭");
    }
    if (value == QStringLiteral("idle")) {
        return QStringLiteral("空闲");
    }
    if (value == QStringLiteral("charging")) {
        return QStringLiteral("充电中");
    }
    if (value == QStringLiteral("fault")) {
        return QStringLiteral("故障");
    }
    if (value == QStringLiteral("offline")) {
        return QStringLiteral("离线");
    }
    if (value == QStringLiteral("reserved")) {
        return QStringLiteral("已预约");
    }
    if (value == QStringLiteral("pending_settlement")) {
        return QStringLiteral("待结算");
    }
    if (value == QStringLiteral("completed")) {
        return QStringLiteral("已完成");
    }
    if (value == QStringLiteral("cancelled")) {
        return QStringLiteral("已取消");
    }
    return value;
}

QString chargerTypeText(const QString &value)
{
    if (value == QStringLiteral("fast")) {
        return QStringLiteral("快充");
    }
    if (value == QStringLiteral("slow")) {
        return QStringLiteral("慢充");
    }
    return value;
}

QString reasonText(const QString &value)
{
    if (value == QStringLiteral("recharge")) {
        return QStringLiteral("余额充值");
    }
    if (value == QStringLiteral("charge_settlement")) {
        return QStringLiteral("充电结算");
    }
    return value;
}

QString appStyleSheet()
{
    return QStringLiteral(R"QSS(
QWidget#UserWindow {
    background: #edf5ff;
    color: #102033;
    font-family: "Microsoft YaHei", "Noto Sans CJK SC", Arial;
    font-size: 10pt;
}
QLabel#headerLabel {
    color: #0f172a;
    font-size: 20px;
    font-weight: 700;
    padding: 6px 10px;
    border-left: 7px solid #1683ff;
    background: #ffffff;
    border-radius: 8px;
}
QGroupBox {
    background: #ffffff;
    border: 1px solid #d6e4f5;
    border-radius: 8px;
    margin-top: 18px;
    padding: 12px 8px 8px 8px;
    font-weight: 700;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 8px;
    color: #0f4ea3;
}
QGroupBox#connectionBox::title { color: #2563eb; }
QGroupBox#stationBox::title { color: #0891b2; }
QGroupBox#chargerBox::title { color: #16a34a; }
QGroupBox#orderBox::title { color: #f97316; }
QGroupBox#dataBox::title { color: #7c3aed; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: #f8fbff;
    border: 1px solid #bdd2ec;
    border-radius: 6px;
    padding: 5px 7px;
    selection-background-color: #1683ff;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid #1683ff;
    background: #ffffff;
}
QPushButton {
    background: #edf5ff;
    border: 1px solid #9bc4ff;
    border-radius: 7px;
    color: #0754b8;
    padding: 7px 14px;
    font-weight: 600;
}
QPushButton:hover { background: #dcecff; }
QPushButton:pressed { background: #c7ddff; }
QPushButton#createOrderButton, QPushButton#rechargeButton {
    background: #10b981;
    border-color: #10b981;
    color: #ffffff;
}
QPushButton#stopOrderButton {
    background: #ef4444;
    border-color: #ef4444;
    color: #ffffff;
}
QPushButton#settleOrderButton {
    background: #f59e0b;
    border-color: #f59e0b;
    color: #ffffff;
}
QTableWidget {
    background: #ffffff;
    alternate-background-color: #f5f9ff;
    border: 1px solid #d8e6f7;
    border-radius: 6px;
    gridline-color: #e7eef8;
}
QHeaderView::section {
    background: #eef6ff;
    color: #17406d;
    border: 0;
    border-right: 1px solid #d5e4f6;
    padding: 6px;
    font-weight: 700;
}
QTableWidget::item:selected {
    background: #d9ecff;
    color: #0f172a;
}
QLabel#currentOrderLabel, QLabel#profileLabel {
    background: #f8fbff;
    border: 1px solid #d6e4f5;
    border-radius: 6px;
    padding: 6px 8px;
    color: #0f4ea3;
}
)QSS");
}

} // namespace

class UserWindow final : public QWidget {
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

        setWindowTitle(QStringLiteral("汽车充电用户端"));
        setStyleSheet(appStyleSheet());
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
        stationTable_->setAlternatingRowColors(true);
        chargerTable_->horizontalHeader()->setStretchLastSection(true);
        chargerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        chargerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        chargerTable_->setAlternatingRowColors(true);
        orderTable_->horizontalHeader()->setStretchLastSection(true);
        orderTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        orderTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        orderTable_->setAlternatingRowColors(true);
        balanceLogTable_->horizontalHeader()->setStretchLastSection(true);
        balanceLogTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        balanceLogTable_->setAlternatingRowColors(true);
        telemetryTable_->horizontalHeader()->setStretchLastSection(true);
        telemetryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        telemetryTable_->setAlternatingRowColors(true);
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
            throw QStringLiteral("连接服务器失败：%1").arg(socket.errorString());
        }

        payload.insert(QStringLiteral("request_id"), ++requestId_);
        socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
        if (!socket.waitForBytesWritten(3000)) {
            throw QStringLiteral("发送请求失败：%1").arg(socket.errorString());
        }
        if (!socket.waitForReadyRead(5000)) {
            throw QStringLiteral("读取响应超时：%1").arg(socket.errorString());
        }

        const QByteArray line = socket.readLine();
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            throw QStringLiteral("服务器响应格式无效：%1").arg(QString::fromUtf8(line));
        }

        const QJsonObject response = doc.object();
        appendLog(QStringLiteral(">> %1").arg(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))));
        appendLog(QStringLiteral("<< %1").arg(QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact))));
        if (!response.value(QStringLiteral("success")).toBool()) {
            throw response.value(QStringLiteral("error")).toString(QStringLiteral("请求失败"));
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
            appendLog(QStringLiteral("[错误] %1").arg(message));
        }
    }

    void login()
    {
        runAction(QStringLiteral("登录失败"), [this]() {
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
            profileLabel_->setText(QStringLiteral("%1  余额：%2 元")
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
        runAction(QStringLiteral("资料更新失败"), [this]() {
            QJsonObject payload = authedPayload(QStringLiteral("user.update_profile"));
            payload.insert(QStringLiteral("nickname"), nicknameEdit_->text().trimmed());
            const QJsonObject user = request(payload).value(QStringLiteral("user")).toObject();
            profileLabel_->setText(QStringLiteral("%1  余额：%2 元")
                                       .arg(user.value(QStringLiteral("phone")).toString())
                                       .arg(moneyText(user.value(QStringLiteral("balance_cents")).toInt())));
        });
    }

    void refreshStations()
    {
        runAction(QStringLiteral("充电站查询失败"), [this]() {
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
                setItem(stationTable_, row, 3, statusText(station.value(QStringLiteral("status")).toString()), id);
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
        runAction(QStringLiteral("充电桩查询失败"), [this, stationId]() {
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
                setItem(chargerTable_, row, 2, chargerTypeText(charger.value(QStringLiteral("type")).toString()), id);
                setItem(chargerTable_, row, 3, QString::number(charger.value(QStringLiteral("power_kw")).toDouble()), id);
                setItem(chargerTable_, row, 4, statusText(charger.value(QStringLiteral("status")).toString()), id);
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
            QMessageBox::information(this, QStringLiteral("预约充电"), QStringLiteral("请先选择一个充电桩。"));
            return;
        }
        runAction(QStringLiteral("预约失败"), [this, chargerId]() {
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
        runOrderAction(QStringLiteral("order.cancel"), QStringLiteral("取消订单失败"));
    }

    void startOrder()
    {
        runOrderAction(QStringLiteral("order.start"), QStringLiteral("开始充电失败"));
    }

    void stopOrder()
    {
        if (!ensureLogin() || !ensureCurrentOrder()) {
            return;
        }
        runAction(QStringLiteral("停止充电失败"), [this]() {
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
        runOrderAction(QStringLiteral("order.settle"), QStringLiteral("订单结算失败"));
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
        runAction(QStringLiteral("当前订单刷新失败"), [this]() {
            const QJsonObject data = request(authedPayload(QStringLiteral("order.current")));
            const QJsonValue value = data.value(QStringLiteral("order"));
            if (value.isNull() || !value.isObject()) {
                currentOrderId_ = 0;
                currentOrderLabel_->setText(QStringLiteral("暂无进行中的订单"));
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
        runAction(QStringLiteral("订单查询失败"), [this]() {
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
                setItem(orderTable_, row, 4, statusText(order.value(QStringLiteral("status")).toString()), id);
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
        runAction(QStringLiteral("充值失败"), [this]() {
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
        profileLabel_->setText(QStringLiteral("%1  余额：%2 元")
                                   .arg(user.value(QStringLiteral("phone")).toString())
                                   .arg(moneyText(user.value(QStringLiteral("balance_cents")).toInt())));
    }

    void refreshBalanceLogs()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("余额流水查询失败"), [this]() {
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
                setItem(balanceLogTable_, row, 3, reasonText(log.value(QStringLiteral("reason")).toString()), id);
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
        runAction(QStringLiteral("遥测数据查询失败"), [this, chargerId]() {
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
                setItem(telemetryTable_, row, 1, statusText(record.value(QStringLiteral("status")).toString()), id);
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
        QString text = QStringLiteral("订单 #%1  %2  充电桩：%3  电量：%4 kWh  金额：%5 元")
                           .arg(currentOrderId_)
                           .arg(statusText(order.value(QStringLiteral("status")).toString()))
                           .arg(order.value(QStringLiteral("charger_code")).toString())
                           .arg(order.value(QStringLiteral("energy_kwh")).toDouble(), 0, 'f', 2)
                           .arg(moneyText(order.value(QStringLiteral("amount_cents")).toInt()));
        if (!extra.isEmpty() && extra.contains(QStringLiteral("estimated_amount_cents"))) {
            text += QStringLiteral("  预计费用：%1 元")
                        .arg(moneyText(extra.value(QStringLiteral("estimated_amount_cents")).toInt()));
        }
        currentOrderLabel_->setText(text);
    }

    bool ensureLogin()
    {
        if (sessionToken_.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("需要登录"), QStringLiteral("请先登录用户账户。"));
            return false;
        }
        return true;
    }

    bool ensureCurrentOrder()
    {
        if (currentOrderId_ <= 0) {
            QMessageBox::information(this, QStringLiteral("需要订单"), QStringLiteral("当前没有进行中的订单，请先预约或刷新当前订单。"));
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
