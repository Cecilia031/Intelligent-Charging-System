#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
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
#include <QTabWidget>
#include <QTcpSocket>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <functional>

#include "ui_admin_client.h"

#ifdef HAS_QT_WEBENGINE
#include <QWebEngineView>
#endif

namespace {

QString moneyText(int cents)
{
    return QStringLiteral("%1").arg(cents / 100.0, 0, 'f', 2);
}

int currentId(QTableWidget *table)
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    if (selected.isEmpty()) {
        return 0;
    }
    return table->item(selected.first()->row(), 0)->data(Qt::UserRole).toInt();
}

QString currentText(QTableWidget *table, int column)
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    if (selected.isEmpty()) {
        return {};
    }
    QTableWidgetItem *item = table->item(selected.first()->row(), column);
    return item ? item->text() : QString();
}

QString currentValue(QTableWidget *table, int column)
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    if (selected.isEmpty()) {
        return {};
    }
    QTableWidgetItem *item = table->item(selected.first()->row(), column);
    if (!item) {
        return {};
    }
    const QVariant value = item->data(Qt::UserRole + 1);
    return value.isValid() ? value.toString() : item->text();
}

void setItem(QTableWidget *table, int row, int column, const QString &text, int id = 0,
             const QString &value = QString())
{
    auto *item = new QTableWidgetItem(text);
    if (id > 0) {
        item->setData(Qt::UserRole, id);
    }
    if (!value.isEmpty()) {
        item->setData(Qt::UserRole + 1, value);
    }
    table->setItem(row, column, item);
}

QString comboValue(const QComboBox *combo)
{
    const QVariant data = combo->currentData();
    return data.isValid() ? data.toString() : combo->currentText();
}

void setComboByValue(QComboBox *combo, const QString &value)
{
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString() == value || combo->itemText(i) == value) {
            combo->setCurrentIndex(i);
            return;
        }
    }
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

QString dashboardStatusName(const QString &value)
{
    return statusText(value);
}

QString appStyleSheet()
{
    return QStringLiteral(R"QSS(
QWidget#AdminWindow {
    background: #eef5ff;
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
QTabWidget::pane {
    background: #ffffff;
    border: 1px solid #d6e4f5;
    border-radius: 8px;
    top: -1px;
}
QTabBar::tab {
    background: #f6faff;
    border: 1px solid #d6e4f5;
    border-bottom: 0;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
    min-width: 78px;
    padding: 8px 12px;
    color: #17406d;
}
QTabBar::tab:selected {
    background: #ffffff;
    color: #0754b8;
    font-weight: 700;
}
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
QPushButton#loginButton, QPushButton#refreshDashboardButton, QPushButton#generateForecastButton {
    background: #1683ff;
    border-color: #1683ff;
    color: #ffffff;
}
QPushButton#createStationButton, QPushButton#createChargerButton, QPushButton#activateUserButton {
    background: #10b981;
    border-color: #10b981;
    color: #ffffff;
}
QPushButton#freezeUserButton, QPushButton#closeStationButton, QPushButton#setChargerStatusButton {
    background: #f59e0b;
    border-color: #f59e0b;
    color: #ffffff;
}
QPushButton#stopOrderButton {
    background: #ef4444;
    border-color: #ef4444;
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
QLabel#adminLabel, QLabel#dashboardStatusLabel {
    background: #f8fbff;
    border: 1px solid #d6e4f5;
    border-radius: 6px;
    padding: 6px 8px;
    color: #0f4ea3;
}
)QSS");
}

QString javascriptJson(const QJsonDocument &document)
{
    QString json = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    json.replace(QStringLiteral("<"), QStringLiteral("\\u003c"));
    json.replace(QStringLiteral(">"), QStringLiteral("\\u003e"));
    json.replace(QStringLiteral("&"), QStringLiteral("\\u0026"));
    json.replace(QChar(0x2028), QStringLiteral("\\u2028"));
    json.replace(QChar(0x2029), QStringLiteral("\\u2029"));
    return json;
}

} // namespace

class AdminWindow final : public QWidget {
public:
    explicit AdminWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        buildUi();
        connectSignals();
    }

    ~AdminWindow() override
    {
        delete ui;
    }

private:
    void buildUi()
    {
        ui = new Ui::AdminWindow;
        ui->setupUi(this);

        setWindowTitle(QStringLiteral("汽车充电运营管理端"));
        setStyleSheet(appStyleSheet());
        resize(1240, 780);

        hostEdit_ = ui->hostEdit;
        portSpin_ = ui->portSpin;
        usernameEdit_ = ui->usernameEdit;
        passwordEdit_ = ui->passwordEdit;
        loginButton_ = ui->loginButton;
        adminLabel_ = ui->adminLabel;
        tabs_ = ui->tabs;
        logEdit_ = ui->logEdit;
        dashboardWebContainer_ = ui->dashboardWebContainer;
        dashboardHtmlEdit_ = ui->dashboardHtmlEdit;
        dashboardStatusLabel_ = ui->dashboardStatusLabel;
        refreshDashboardButton_ = ui->refreshDashboardButton;
        exportDashboardButton_ = ui->exportDashboardButton;

        userKeywordEdit_ = ui->userKeywordEdit;
        userStatusCombo_ = ui->userStatusCombo;
        refreshUsersButton_ = ui->refreshUsersButton;
        freezeUserButton_ = ui->freezeUserButton;
        activateUserButton_ = ui->activateUserButton;
        userTable_ = ui->userTable;

        stationNameEdit_ = ui->stationNameEdit;
        stationAddressEdit_ = ui->stationAddressEdit;
        stationLatSpin_ = ui->stationLatSpin;
        stationLngSpin_ = ui->stationLngSpin;
        stationStatusCombo_ = ui->stationStatusCombo;
        refreshStationsButton_ = ui->refreshStationsButton;
        createStationButton_ = ui->createStationButton;
        updateStationButton_ = ui->updateStationButton;
        openStationButton_ = ui->openStationButton;
        closeStationButton_ = ui->closeStationButton;
        stationTable_ = ui->stationTable;

        chargerStationIdSpin_ = ui->chargerStationIdSpin;
        chargerCodeEdit_ = ui->chargerCodeEdit;
        chargerTypeCombo_ = ui->chargerTypeCombo;
        chargerPowerSpin_ = ui->chargerPowerSpin;
        chargerStatusCombo_ = ui->chargerStatusCombo;
        refreshChargersButton_ = ui->refreshChargersButton;
        createChargerButton_ = ui->createChargerButton;
        updateChargerButton_ = ui->updateChargerButton;
        setChargerStatusButton_ = ui->setChargerStatusButton;
        chargerTable_ = ui->chargerTable;
        telemetryTable_ = ui->telemetryTable;

        orderUserIdSpin_ = ui->orderUserIdSpin;
        orderStatusCombo_ = ui->orderStatusCombo;
        refreshOrdersButton_ = ui->refreshOrdersButton;
        stopOrderButton_ = ui->stopOrderButton;
        settleOrderButton_ = ui->settleOrderButton;
        orderEnergySpin_ = ui->orderEnergySpin;
        orderTable_ = ui->orderTable;

        overviewStationIdSpin_ = ui->overviewStationIdSpin;
        overviewHorizonCombo_ = ui->overviewHorizonCombo;
        refreshOverviewButton_ = ui->refreshOverviewButton;
        generateForecastButton_ = ui->generateForecastButton;
        refreshForecastsButton_ = ui->refreshForecastsButton;
        summaryOrderCountLabel_ = ui->summaryOrderCountLabel;
        summaryEnergyLabel_ = ui->summaryEnergyLabel;
        summaryRevenueLabel_ = ui->summaryRevenueLabel;
        summaryAverageEnergyLabel_ = ui->summaryAverageEnergyLabel;
        summaryLoadLabel_ = ui->summaryLoadLabel;
        summaryUsersLabel_ = ui->summaryUsersLabel;
        summaryStationsLabel_ = ui->summaryStationsLabel;
        summaryChargersLabel_ = ui->summaryChargersLabel;
        forecastTable_ = ui->forecastTable;
        orderStatusTable_ = ui->orderStatusTable;
        chargerStatusOverviewTable_ = ui->chargerStatusOverviewTable;

        setupProtocolCombos();
        setupTable(userTable_);
        setupTable(stationTable_);
        setupTable(chargerTable_);
        setupTable(telemetryTable_);
        setupTable(orderTable_);
        setupTable(forecastTable_);
        setupTable(orderStatusTable_);
        setupTable(chargerStatusOverviewTable_);
        logEdit_->setReadOnly(true);
        logEdit_->setMaximumBlockCount(400);
        dashboardHtmlEdit_->setReadOnly(true);
        dashboardHtmlEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
#ifdef HAS_QT_WEBENGINE
        dashboardHtmlEdit_->hide();
        dashboardWebContainer_->setMinimumHeight(500);
        dashboardStatusLabel_->setText(QStringLiteral("可视化大屏已就绪（嵌入图表模式）"));
#else
        dashboardWebContainer_->hide();
        dashboardHtmlEdit_->setMinimumHeight(500);
        dashboardStatusLabel_->setText(QStringLiteral("网页预览模式（当前环境未启用嵌入图表组件）"));
#endif
    }

    void setupProtocolCombos()
    {
        userStatusCombo_->setItemData(0, QStringLiteral("all"));
        userStatusCombo_->setItemData(1, QStringLiteral("active"));
        userStatusCombo_->setItemData(2, QStringLiteral("frozen"));

        stationStatusCombo_->setItemData(0, QStringLiteral("open"));
        stationStatusCombo_->setItemData(1, QStringLiteral("closed"));

        chargerTypeCombo_->setItemData(0, QStringLiteral("fast"));
        chargerTypeCombo_->setItemData(1, QStringLiteral("slow"));

        chargerStatusCombo_->setItemData(0, QStringLiteral("idle"));
        chargerStatusCombo_->setItemData(1, QStringLiteral("charging"));
        chargerStatusCombo_->setItemData(2, QStringLiteral("fault"));
        chargerStatusCombo_->setItemData(3, QStringLiteral("offline"));

        overviewHorizonCombo_->setItemData(0, QStringLiteral("all"));
        overviewHorizonCombo_->setItemData(1, QStringLiteral("1"));
        overviewHorizonCombo_->setItemData(2, QStringLiteral("6"));
        overviewHorizonCombo_->setItemData(3, QStringLiteral("24"));

        orderStatusCombo_->setItemData(0, QStringLiteral("all"));
        orderStatusCombo_->setItemData(1, QStringLiteral("reserved"));
        orderStatusCombo_->setItemData(2, QStringLiteral("charging"));
        orderStatusCombo_->setItemData(3, QStringLiteral("pending_settlement"));
        orderStatusCombo_->setItemData(4, QStringLiteral("completed"));
        orderStatusCombo_->setItemData(5, QStringLiteral("cancelled"));
    }

    void setupTable(QTableWidget *table)
    {
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
    }

    void connectSignals()
    {
        connect(loginButton_, &QPushButton::clicked, this, &AdminWindow::login);
        connect(refreshUsersButton_, &QPushButton::clicked, this, &AdminWindow::refreshUsers);
        connect(freezeUserButton_, &QPushButton::clicked, this, [this]() { setUserStatus(QStringLiteral("frozen")); });
        connect(activateUserButton_, &QPushButton::clicked, this, [this]() { setUserStatus(QStringLiteral("active")); });
        connect(refreshStationsButton_, &QPushButton::clicked, this, &AdminWindow::refreshStations);
        connect(createStationButton_, &QPushButton::clicked, this, &AdminWindow::createStation);
        connect(updateStationButton_, &QPushButton::clicked, this, &AdminWindow::updateStation);
        connect(openStationButton_, &QPushButton::clicked, this, [this]() { setStationStatus(QStringLiteral("open")); });
        connect(closeStationButton_, &QPushButton::clicked, this, [this]() { setStationStatus(QStringLiteral("closed")); });
        connect(stationTable_, &QTableWidget::itemSelectionChanged, this, &AdminWindow::stationSelected);
        connect(refreshChargersButton_, &QPushButton::clicked, this, &AdminWindow::refreshChargers);
        connect(createChargerButton_, &QPushButton::clicked, this, &AdminWindow::createCharger);
        connect(updateChargerButton_, &QPushButton::clicked, this, &AdminWindow::updateCharger);
        connect(setChargerStatusButton_, &QPushButton::clicked, this, &AdminWindow::setChargerStatus);
        connect(chargerTable_, &QTableWidget::itemSelectionChanged, this, &AdminWindow::chargerSelected);
        connect(refreshOverviewButton_, &QPushButton::clicked, this, &AdminWindow::refreshOverview);
        connect(generateForecastButton_, &QPushButton::clicked, this, &AdminWindow::generateForecast);
        connect(refreshForecastsButton_, &QPushButton::clicked, this, &AdminWindow::refreshForecasts);
        connect(refreshDashboardButton_, &QPushButton::clicked, this, &AdminWindow::refreshDashboard);
        connect(exportDashboardButton_, &QPushButton::clicked, this, &AdminWindow::exportDashboardHtml);
        connect(refreshOrdersButton_, &QPushButton::clicked, this, &AdminWindow::refreshOrders);
        connect(stopOrderButton_, &QPushButton::clicked, this, &AdminWindow::stopOrder);
        connect(settleOrderButton_, &QPushButton::clicked, this, &AdminWindow::settleOrder);
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
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
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

    QJsonObject authed(const QString &action) const
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("action"), action);
        payload.insert(QStringLiteral("session_token"), sessionToken_);
        return payload;
    }

    void runAction(const QString &title, const std::function<void()> &action)
    {
        try {
            action();
        } catch (const QString &message) {
            QMessageBox::warning(this, title, message);
            appendLog(QStringLiteral("[错误] %1").arg(message));
        }
    }

    bool ensureLogin()
    {
        if (sessionToken_.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("需要登录"), QStringLiteral("请先登录管理员账户。"));
            return false;
        }
        return true;
    }

    void login()
    {
        runAction(QStringLiteral("登录失败"), [this]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("admin.login"));
            payload.insert(QStringLiteral("username"), usernameEdit_->text().trimmed());
            payload.insert(QStringLiteral("password"), passwordEdit_->text());
            const QJsonObject data = request(payload);
            const QJsonObject admin = data.value(QStringLiteral("admin")).toObject();
            sessionToken_ = data.value(QStringLiteral("session")).toObject().value(QStringLiteral("token")).toString();
            adminLabel_->setText(QStringLiteral("%1 (%2)")
                                     .arg(admin.value(QStringLiteral("display_name")).toString())
                                     .arg(admin.value(QStringLiteral("username")).toString()));
            refreshUsers();
            refreshStations();
            refreshChargers();
            refreshOverview();
            refreshForecasts();
            refreshDashboard();
            refreshOrders();
        });
    }

    void refreshUsers()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("用户查询失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("admin.user.list"));
            const QString keyword = userKeywordEdit_->text().trimmed();
            if (!keyword.isEmpty()) {
                payload.insert(QStringLiteral("keyword"), keyword);
            }
            const QString userStatus = comboValue(userStatusCombo_);
            if (userStatus != QStringLiteral("all")) {
                payload.insert(QStringLiteral("status"), userStatus);
            }
            const QJsonArray users = request(payload).value(QStringLiteral("users")).toArray();
            userTable_->setRowCount(users.size());
            for (int row = 0; row < users.size(); ++row) {
                const QJsonObject user = users.at(row).toObject();
                const int id = user.value(QStringLiteral("id")).toInt();
                setItem(userTable_, row, 0, QString::number(id), id);
                setItem(userTable_, row, 1, user.value(QStringLiteral("phone")).toString(), id);
                setItem(userTable_, row, 2, user.value(QStringLiteral("nickname")).toString(), id);
                setItem(userTable_, row, 3, moneyText(user.value(QStringLiteral("balance_cents")).toInt()), id);
                const QString status = user.value(QStringLiteral("status")).toString();
                setItem(userTable_, row, 4, statusText(status), id, status);
            }
            userTable_->resizeColumnsToContents();
        });
    }

    void setUserStatus(const QString &status)
    {
        if (!ensureLogin()) {
            return;
        }
        const int userId = currentId(userTable_);
        if (userId <= 0) {
            QMessageBox::information(this, QStringLiteral("用户管理"), QStringLiteral("请先选择一个用户。"));
            return;
        }
        runAction(QStringLiteral("用户状态更新失败"), [this, userId, status]() {
            QJsonObject payload = authed(QStringLiteral("admin.user.set_status"));
            payload.insert(QStringLiteral("user_id"), userId);
            payload.insert(QStringLiteral("status"), status);
            request(payload);
            refreshUsers();
        });
    }

    void refreshStations()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("充电站查询失败"), [this]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("station.list"));
            payload.insert(QStringLiteral("include_closed"), true);
            const QJsonArray stations = request(payload).value(QStringLiteral("stations")).toArray();
            stationTable_->setRowCount(stations.size());
            for (int row = 0; row < stations.size(); ++row) {
                const QJsonObject station = stations.at(row).toObject();
                const int id = station.value(QStringLiteral("id")).toInt();
                setItem(stationTable_, row, 0, QString::number(id), id);
                setItem(stationTable_, row, 1, station.value(QStringLiteral("name")).toString(), id);
                setItem(stationTable_, row, 2, station.value(QStringLiteral("address")).toString(), id);
                const QString status = station.value(QStringLiteral("status")).toString();
                setItem(stationTable_, row, 3, statusText(status), id, status);
            }
            stationTable_->resizeColumnsToContents();
        });
    }

    void stationSelected()
    {
        const int stationId = currentId(stationTable_);
        if (stationId <= 0) {
            return;
        }
        stationNameEdit_->setText(currentText(stationTable_, 1));
        stationAddressEdit_->setText(currentText(stationTable_, 2));
        setComboByValue(stationStatusCombo_, currentValue(stationTable_, 3));
        chargerStationIdSpin_->setValue(stationId);
    }

    QJsonObject stationPayload(const QString &action)
    {
        QJsonObject payload = authed(action);
        payload.insert(QStringLiteral("name"), stationNameEdit_->text().trimmed());
        payload.insert(QStringLiteral("address"), stationAddressEdit_->text().trimmed());
        payload.insert(QStringLiteral("latitude"), stationLatSpin_->value());
        payload.insert(QStringLiteral("longitude"), stationLngSpin_->value());
        payload.insert(QStringLiteral("status"), comboValue(stationStatusCombo_));
        return payload;
    }

    void createStation()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("新增充电站失败"), [this]() {
            request(stationPayload(QStringLiteral("admin.station.create")));
            refreshStations();
        });
    }

    void updateStation()
    {
        if (!ensureLogin()) {
            return;
        }
        const int stationId = currentId(stationTable_);
        if (stationId <= 0) {
            QMessageBox::information(this, QStringLiteral("充电站管理"), QStringLiteral("请先选择一个充电站。"));
            return;
        }
        runAction(QStringLiteral("更新充电站失败"), [this, stationId]() {
            QJsonObject payload = stationPayload(QStringLiteral("admin.station.update"));
            payload.insert(QStringLiteral("station_id"), stationId);
            request(payload);
            refreshStations();
        });
    }

    void setStationStatus(const QString &status)
    {
        if (!ensureLogin()) {
            return;
        }
        const int stationId = currentId(stationTable_);
        if (stationId <= 0) {
            QMessageBox::information(this, QStringLiteral("充电站管理"), QStringLiteral("请先选择一个充电站。"));
            return;
        }
        runAction(QStringLiteral("充电站状态更新失败"), [this, stationId, status]() {
            QJsonObject payload = authed(QStringLiteral("admin.station.set_status"));
            payload.insert(QStringLiteral("station_id"), stationId);
            payload.insert(QStringLiteral("status"), status);
            request(payload);
            refreshStations();
        });
    }

    void refreshChargers()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("充电桩查询失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("admin.charger.list"));
            if (chargerStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), chargerStationIdSpin_->value());
            }
            const QJsonArray chargers = request(payload).value(QStringLiteral("chargers")).toArray();
            chargerTable_->setRowCount(chargers.size());
            for (int row = 0; row < chargers.size(); ++row) {
                const QJsonObject charger = chargers.at(row).toObject();
                const int id = charger.value(QStringLiteral("id")).toInt();
                setItem(chargerTable_, row, 0, QString::number(id), id);
                setItem(chargerTable_, row, 1, QString::number(charger.value(QStringLiteral("station_id")).toInt()), id);
                setItem(chargerTable_, row, 2, charger.value(QStringLiteral("code")).toString(), id);
                const QString type = charger.value(QStringLiteral("type")).toString();
                setItem(chargerTable_, row, 3, chargerTypeText(type), id, type);
                setItem(chargerTable_, row, 4, QString::number(charger.value(QStringLiteral("power_kw")).toDouble(), 'f', 2), id);
                const QString status = charger.value(QStringLiteral("status")).toString();
                setItem(chargerTable_, row, 5, statusText(status), id, status);
                setItem(chargerTable_, row, 6, QString::number(charger.value(QStringLiteral("total_orders")).toInt()), id);
            }
            chargerTable_->resizeColumnsToContents();
        });
    }

    void chargerSelected()
    {
        const int chargerId = currentId(chargerTable_);
        if (chargerId <= 0) {
            return;
        }
        chargerStationIdSpin_->setValue(currentText(chargerTable_, 1).toInt());
        chargerCodeEdit_->setText(currentText(chargerTable_, 2));
        setComboByValue(chargerTypeCombo_, currentValue(chargerTable_, 3));
        chargerPowerSpin_->setValue(currentText(chargerTable_, 4).toDouble());
        setComboByValue(chargerStatusCombo_, currentValue(chargerTable_, 5));
        refreshTelemetry(chargerId);
    }

    QJsonObject chargerPayload(const QString &action)
    {
        QJsonObject payload = authed(action);
        payload.insert(QStringLiteral("station_id"), chargerStationIdSpin_->value());
        payload.insert(QStringLiteral("code"), chargerCodeEdit_->text().trimmed());
        payload.insert(QStringLiteral("type"), comboValue(chargerTypeCombo_));
        payload.insert(QStringLiteral("power_kw"), chargerPowerSpin_->value());
        payload.insert(QStringLiteral("status"), comboValue(chargerStatusCombo_));
        return payload;
    }

    void createCharger()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("新增充电桩失败"), [this]() {
            request(chargerPayload(QStringLiteral("admin.charger.create")));
            refreshChargers();
        });
    }

    void updateCharger()
    {
        const int chargerId = currentId(chargerTable_);
        if (!ensureLogin() || chargerId <= 0) {
            return;
        }
        runAction(QStringLiteral("更新充电桩失败"), [this, chargerId]() {
            QJsonObject payload = chargerPayload(QStringLiteral("admin.charger.update"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            request(payload);
            refreshChargers();
        });
    }

    void setChargerStatus()
    {
        const int chargerId = currentId(chargerTable_);
        if (!ensureLogin() || chargerId <= 0) {
            return;
        }
        runAction(QStringLiteral("充电桩状态更新失败"), [this, chargerId]() {
            QJsonObject payload = authed(QStringLiteral("admin.charger.set_status"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            payload.insert(QStringLiteral("status"), comboValue(chargerStatusCombo_));
            request(payload);
            refreshChargers();
        });
    }

    void refreshTelemetry(int chargerId)
    {
        runAction(QStringLiteral("遥测数据查询失败"), [this, chargerId]() {
            QJsonObject payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("telemetry.list"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            payload.insert(QStringLiteral("limit"), 30);
            const QJsonArray rows = request(payload).value(QStringLiteral("telemetry")).toArray();
            telemetryTable_->setRowCount(rows.size());
            for (int row = 0; row < rows.size(); ++row) {
                const QJsonObject record = rows.at(row).toObject();
                const int id = record.value(QStringLiteral("id")).toInt();
                setItem(telemetryTable_, row, 0, QString::number(id), id);
                const QString status = record.value(QStringLiteral("status")).toString();
                setItem(telemetryTable_, row, 1, statusText(status), id, status);
                setItem(telemetryTable_, row, 2, QString::number(record.value(QStringLiteral("power_kw")).toDouble(), 'f', 2), id);
                setItem(telemetryTable_, row, 3, QString::number(record.value(QStringLiteral("energy_kwh")).toDouble(), 'f', 2), id);
                setItem(telemetryTable_, row, 4, record.value(QStringLiteral("recorded_at")).toString(), id);
            }
            telemetryTable_->resizeColumnsToContents();
        });
    }

    void refreshOverview()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("统计概览刷新失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("statistics.overview"));
            if (overviewStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
            }
            const QJsonObject data = request(payload);
            summaryOrderCountLabel_->setText(QString::number(data.value(QStringLiteral("order_count")).toInt()));
            summaryEnergyLabel_->setText(QString::number(data.value(QStringLiteral("completed_energy_kwh")).toDouble(), 'f', 2));
            summaryRevenueLabel_->setText(moneyText(static_cast<int>(data.value(QStringLiteral("revenue_cents")).toDouble())));
            summaryAverageEnergyLabel_->setText(QString::number(data.value(QStringLiteral("average_energy_kwh")).toDouble(), 'f', 2));
            summaryLoadLabel_->setText(QString::number(data.value(QStringLiteral("current_load_kw")).toDouble(), 'f', 2));
            summaryUsersLabel_->setText(QString::number(data.value(QStringLiteral("user_count")).toInt()));
            summaryStationsLabel_->setText(QString::number(data.value(QStringLiteral("station_count")).toInt()));
            summaryChargersLabel_->setText(QString::number(data.value(QStringLiteral("charger_count")).toInt()));

            fillStatusTable(orderStatusTable_, data.value(QStringLiteral("order_statuses")).toArray(), QStringLiteral("count"));
            fillChargerStatusTable(chargerStatusOverviewTable_, data.value(QStringLiteral("charger_statuses")).toArray());
        });
    }

    QString dashboardHtml(const QJsonObject &overview, const QJsonArray &forecasts,
                          const QJsonArray &actualLoads) const
    {
        const QString orderCount = QString::number(overview.value(QStringLiteral("order_count")).toInt());
        const QString completedEnergy = QString::number(overview.value(QStringLiteral("completed_energy_kwh")).toDouble(), 'f', 2);
        const QString revenueYuan = QString::number(overview.value(QStringLiteral("revenue_yuan")).toDouble(), 'f', 2);
        const QString loadKw = QString::number(overview.value(QStringLiteral("current_load_kw")).toDouble(), 'f', 2);
        const QString summaryJson = javascriptJson(QJsonDocument(overview));
        const QString forecastsJson = javascriptJson(QJsonDocument(forecasts));
        const QString actualLoadsJson = javascriptJson(QJsonDocument(actualLoads));
        QString html = QStringLiteral(R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net;">
  <script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
  <style>
    body { margin: 0; background: #eef5ff; color: #102033; font-family: "Microsoft YaHei", Arial, sans-serif; }
    .wrap { padding: 16px; display: grid; gap: 12px; grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .card { background: #fff; border: 1px solid #d6e4f5; border-radius: 8px; padding: 14px; box-shadow: 0 4px 14px rgba(28, 85, 145, .10); }
    .title { font-size: 14px; color: #5d7290; }
    .value { font-size: 28px; font-weight: 700; margin-top: 6px; color: #1683ff; }
    .card:nth-child(2) .value { color: #10b981; }
    .card:nth-child(3) .value { color: #f59e0b; }
    .card:nth-child(4) .value { color: #7c3aed; }
    .panel { background: #fff; border: 1px solid #d6e4f5; border-radius: 8px; padding: 12px; min-height: 320px; box-shadow: 0 4px 14px rgba(28, 85, 145, .08); }
    #chart, #loadChart { width: 100%; height: 320px; }
    .chart-fallback { box-sizing: border-box; display: flex; align-items: center; justify-content: center; height: 320px; padding: 24px; color: #f59e0b; text-align: center; }
    pre { white-space: pre-wrap; word-break: break-word; color: #334155; font-size: 12px; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card"><div class="title">订单总数</div><div class="value">%1</div></div>
    <div class="card"><div class="title">完成电量(kWh)</div><div class="value">%2</div></div>
    <div class="card"><div class="title">营业收入(元)</div><div class="value">%3</div></div>
    <div class="card"><div class="title">当前负载(kW)</div><div class="value">%4</div></div>
    <div class="panel" style="grid-column: span 2;">
      <div id="chart"></div>
    </div>
    <div class="panel" style="grid-column: span 2;">
      <div id="loadChart"></div>
    </div>
    <div class="panel" style="grid-column: span 4;">
      <div class="title">大屏原始数据 JSON</div>
      <pre id="dashboardJson"></pre>
    </div>
  </div>
  <script>
    const overview = %5;
    const forecasts = %6;
    const actualLoads = %7;
    const statusNames = {
      reserved: '已预约',
      charging: '充电中',
      pending_settlement: '待结算',
      completed: '已完成',
      cancelled: '已取消',
      idle: '空闲',
      fault: '故障',
      offline: '离线',
      active: '正常',
      frozen: '冻结'
    };
    const statusText = value => statusNames[value] || value;
    document.getElementById('dashboardJson').textContent = JSON.stringify(
      { overview, actual_load_history: actualLoads, forecasts }, null, 2);
    const orderStatuses = overview.order_statuses || [];
    const chartMessage = (id, message) => {
      const element = document.getElementById(id);
      element.className = 'chart-fallback';
      element.textContent = message;
    };
    if (!window.echarts) {
      chartMessage('chart', 'ECharts 未能加载。连接网络后刷新大屏即可显示图表。');
      chartMessage('loadChart', '实际负载和预测数据仍保留在下方导出的 HTML 数据中。');
    } else {
      const orderChart = echarts.init(document.getElementById('chart'));
      orderChart.setOption({
        backgroundColor: 'transparent',
        title: { text: '订单状态分布', textStyle: { color: '#102033' } },
        tooltip: {},
        xAxis: { type: 'category', data: orderStatuses.map(x => statusText(x.status)), axisLabel: { color: '#64748b' } },
        yAxis: { type: 'value', axisLabel: { color: '#64748b' } },
        series: [{ type: 'bar', data: orderStatuses.map(x => x.count), itemStyle: { color: '#1683ff' } }]
      });
      const forecastTime = point => {
        const generatedAt = new Date(point.generated_at).getTime();
        return Number.isNaN(generatedAt)
          ? `${point.generated_at} / +${point.horizon_hours}h`
          : new Date(generatedAt + point.horizon_hours * 3600000).toISOString();
      };
      const actualByTime = new Map(actualLoads.map(point => [point.recorded_at, point.actual_load_kw]));
      const forecastByTime = new Map();
      forecasts.forEach(point => {
        const time = forecastTime(point);
        if (!forecastByTime.has(time)) {
          forecastByTime.set(time, point.predicted_load_kw);
        }
      });
      const timeline = [...new Set([...actualByTime.keys(), ...forecastByTime.keys()])].sort();
      if (timeline.length === 0) {
        chartMessage('loadChart', '当前筛选条件下暂无实际遥测或预测记录。');
      } else {
        const loadChart = echarts.init(document.getElementById('loadChart'));
        loadChart.setOption({
          backgroundColor: 'transparent',
          title: { text: '实际负载与预测负载', textStyle: { color: '#102033' } },
          tooltip: { trigger: 'axis' },
          legend: { data: ['实际负载(kW)', '预测负载(kW)'], textStyle: { color: '#64748b' } },
          xAxis: {
            type: 'category',
            data: timeline,
            axisLabel: { color: '#64748b', rotate: 25, formatter: value => value.replace('T', ' ').slice(5, 16) }
          },
          yAxis: { type: 'value', axisLabel: { color: '#64748b' } },
          series: [
            {
              name: '实际负载(kW)',
              type: 'line',
              smooth: true,
              connectNulls: false,
              data: timeline.map(time => actualByTime.has(time) ? actualByTime.get(time) : null),
              itemStyle: { color: '#10b981' }
            },
            {
              name: '预测负载(kW)',
              type: 'line',
              smooth: true,
              connectNulls: false,
              data: timeline.map(time => forecastByTime.has(time) ? forecastByTime.get(time) : null),
              itemStyle: { color: '#f59e0b' }
            }
          ]
        });
        window.addEventListener('resize', () => { orderChart.resize(); loadChart.resize(); });
      }
    }
  </script>
</body>
</html>
)HTML");
        html.replace(QStringLiteral("%7"), actualLoadsJson);
        html.replace(QStringLiteral("%6"), forecastsJson);
        html.replace(QStringLiteral("%5"), summaryJson);
        html.replace(QStringLiteral("%4"), loadKw);
        html.replace(QStringLiteral("%3"), revenueYuan);
        html.replace(QStringLiteral("%2"), completedEnergy);
        html.replace(QStringLiteral("%1"), orderCount);
        return html;
    }

    QJsonObject dashboardOverview()
    {
        QJsonObject payload = authed(QStringLiteral("statistics.overview"));
        if (overviewStationIdSpin_->value() > 0) {
            payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
        }
        return request(payload);
    }

    QJsonArray dashboardForecasts()
    {
        QJsonObject payload = authed(QStringLiteral("forecast.list"));
        if (overviewStationIdSpin_->value() > 0) {
            payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
        }
        const QString horizonText = comboValue(overviewHorizonCombo_);
        if (horizonText != QStringLiteral("all")) {
            payload.insert(QStringLiteral("horizon_hours"), horizonText.toInt());
        }
        payload.insert(QStringLiteral("limit"), 20);
        return request(payload).value(QStringLiteral("forecasts")).toArray();
    }

    QJsonArray dashboardLoadHistory()
    {
        QJsonObject payload = authed(QStringLiteral("statistics.load_history"));
        if (overviewStationIdSpin_->value() > 0) {
            payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
        }
        payload.insert(QStringLiteral("limit"), 50);
        return request(payload).value(QStringLiteral("samples")).toArray();
    }

    void renderDashboard(const QJsonObject &overview, const QJsonArray &forecasts,
                         const QJsonArray &actualLoads)
    {
        const QString html = dashboardHtml(overview, forecasts, actualLoads);
        dashboardHtmlEdit_->setPlainText(html);
        dashboardStatusLabel_->setText(QStringLiteral("大屏数据已刷新"));
#ifdef HAS_QT_WEBENGINE
        if (!dashboardWebView_) {
            dashboardWebView_ = new QWebEngineView(dashboardWebContainer_);
            auto *layout = new QVBoxLayout(dashboardWebContainer_);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(dashboardWebView_);
        }
        dashboardWebView_->setHtml(html);
#else
        Q_UNUSED(html)
#endif
    }

    void refreshDashboard()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("可视化大屏刷新失败"), [this]() {
            const QJsonObject overview = dashboardOverview();
            const QJsonArray forecasts = dashboardForecasts();
            const QJsonArray actualLoads = dashboardLoadHistory();
            renderDashboard(overview, forecasts, actualLoads);
        });
    }

    void exportDashboardHtml()
    {
        if (dashboardHtmlEdit_->toPlainText().isEmpty()) {
            QMessageBox::information(this, QStringLiteral("可视化大屏"), QStringLiteral("请先刷新大屏数据。"));
            return;
        }
        const QString filePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出大屏网页文件"), QString(), QStringLiteral("网页文件 (*.html)"));
        if (filePath.isEmpty()) {
            return;
        }
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
            return;
        }
        QTextStream stream(&file);
        stream << dashboardHtmlEdit_->toPlainText();
        dashboardStatusLabel_->setText(QStringLiteral("大屏网页文件已导出"));
    }

    void generateForecast()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("生成负载预测失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("forecast.generate"));
            if (overviewStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
            }
            const QString horizonText = comboValue(overviewHorizonCombo_);
            if (horizonText != QStringLiteral("all")) {
                payload.insert(QStringLiteral("horizon_hours"), horizonText.toInt());
            } else {
                payload.insert(QStringLiteral("horizon_hours"), 1);
            }
            request(payload);
            refreshForecasts();
        });
    }

    void refreshForecasts()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("负载预测查询失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("forecast.list"));
            if (overviewStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
            }
            const QString horizonText = comboValue(overviewHorizonCombo_);
            if (horizonText != QStringLiteral("all")) {
                payload.insert(QStringLiteral("horizon_hours"), horizonText.toInt());
            }
            payload.insert(QStringLiteral("limit"), 50);
            const QJsonArray forecasts = request(payload).value(QStringLiteral("forecasts")).toArray();
            forecastTable_->setRowCount(forecasts.size());
            for (int row = 0; row < forecasts.size(); ++row) {
                const QJsonObject forecast = forecasts.at(row).toObject();
                const int id = forecast.value(QStringLiteral("id")).toInt();
                setItem(forecastTable_, row, 0, QString::number(id), id);
                setItem(forecastTable_, row, 1, forecast.value(QStringLiteral("station_name")).toString(), id);
                setItem(forecastTable_, row, 2, QString::number(forecast.value(QStringLiteral("horizon_hours")).toInt()), id);
                setItem(forecastTable_, row, 3, QString::number(forecast.value(QStringLiteral("predicted_load_kw")).toDouble(), 'f', 2), id);
                setItem(forecastTable_, row, 4, forecast.value(QStringLiteral("generated_at")).toString(), id);
            }
            forecastTable_->resizeColumnsToContents();
        });
    }

    void fillStatusTable(QTableWidget *table, const QJsonArray &rows, const QString &countKey)
    {
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row) {
            const QJsonObject item = rows.at(row).toObject();
            const QString status = item.value(QStringLiteral("status")).toString();
            setItem(table, row, 0, dashboardStatusName(status), 0, status);
            setItem(table, row, 1, QString::number(item.value(countKey).toInt()));
        }
        table->resizeColumnsToContents();
    }

    void fillChargerStatusTable(QTableWidget *table, const QJsonArray &rows)
    {
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row) {
            const QJsonObject item = rows.at(row).toObject();
            const QString status = item.value(QStringLiteral("status")).toString();
            setItem(table, row, 0, dashboardStatusName(status), 0, status);
            setItem(table, row, 1, QString::number(item.value(QStringLiteral("count")).toInt()));
            setItem(table, row, 2, QString::number(item.value(QStringLiteral("current_power_kw")).toDouble(), 'f', 2));
        }
        table->resizeColumnsToContents();
    }

    void refreshOrders()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("订单查询失败"), [this]() {
            QJsonObject payload = authed(QStringLiteral("order.list"));
            if (orderUserIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("user_id"), orderUserIdSpin_->value());
            }
            const QString orderStatus = comboValue(orderStatusCombo_);
            if (orderStatus != QStringLiteral("all")) {
                payload.insert(QStringLiteral("status"), orderStatus);
            }
            payload.insert(QStringLiteral("limit"), 200);
            const QJsonArray orders = request(payload).value(QStringLiteral("orders")).toArray();
            orderTable_->setRowCount(orders.size());
            for (int row = 0; row < orders.size(); ++row) {
                const QJsonObject order = orders.at(row).toObject();
                const int id = order.value(QStringLiteral("id")).toInt();
                setItem(orderTable_, row, 0, QString::number(id), id);
                setItem(orderTable_, row, 1, order.value(QStringLiteral("order_no")).toString(), id);
                setItem(orderTable_, row, 2, order.value(QStringLiteral("user_phone")).toString(), id);
                setItem(orderTable_, row, 3, order.value(QStringLiteral("station_name")).toString(), id);
                setItem(orderTable_, row, 4, order.value(QStringLiteral("charger_code")).toString(), id);
                const QString status = order.value(QStringLiteral("status")).toString();
                setItem(orderTable_, row, 5, statusText(status), id, status);
                setItem(orderTable_, row, 6, QString::number(order.value(QStringLiteral("energy_kwh")).toDouble(), 'f', 2), id);
                setItem(orderTable_, row, 7, moneyText(order.value(QStringLiteral("amount_cents")).toInt()), id);
            }
            orderTable_->resizeColumnsToContents();
        });
    }

    void stopOrder()
    {
        const int orderId = currentId(orderTable_);
        if (!ensureLogin() || orderId <= 0) {
            return;
        }
        runAction(QStringLiteral("停止订单失败"), [this, orderId]() {
            QJsonObject payload = authed(QStringLiteral("order.stop"));
            payload.insert(QStringLiteral("order_id"), orderId);
            payload.insert(QStringLiteral("energy_kwh"), orderEnergySpin_->value());
            request(payload);
            refreshOrders();
            refreshChargers();
        });
    }

    void settleOrder()
    {
        const int orderId = currentId(orderTable_);
        if (!ensureLogin() || orderId <= 0) {
            return;
        }
        runAction(QStringLiteral("订单结算失败"), [this, orderId]() {
            QJsonObject payload = authed(QStringLiteral("order.settle"));
            payload.insert(QStringLiteral("order_id"), orderId);
            request(payload);
            refreshOrders();
            refreshUsers();
            refreshChargers();
        });
    }

    void appendLog(const QString &line)
    {
        logEdit_->appendPlainText(QStringLiteral("[%1] %2")
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                      .arg(line.trimmed()));
    }

    QLineEdit *hostEdit_ = nullptr;
    QSpinBox *portSpin_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QLabel *adminLabel_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
    QWidget *dashboardWebContainer_ = nullptr;
    QPlainTextEdit *dashboardHtmlEdit_ = nullptr;
    QLabel *dashboardStatusLabel_ = nullptr;
    QPushButton *refreshDashboardButton_ = nullptr;
    QPushButton *exportDashboardButton_ = nullptr;

    QLineEdit *userKeywordEdit_ = nullptr;
    QComboBox *userStatusCombo_ = nullptr;
    QPushButton *refreshUsersButton_ = nullptr;
    QPushButton *freezeUserButton_ = nullptr;
    QPushButton *activateUserButton_ = nullptr;
    QTableWidget *userTable_ = nullptr;

    QLineEdit *stationNameEdit_ = nullptr;
    QLineEdit *stationAddressEdit_ = nullptr;
    QDoubleSpinBox *stationLatSpin_ = nullptr;
    QDoubleSpinBox *stationLngSpin_ = nullptr;
    QComboBox *stationStatusCombo_ = nullptr;
    QPushButton *refreshStationsButton_ = nullptr;
    QPushButton *createStationButton_ = nullptr;
    QPushButton *updateStationButton_ = nullptr;
    QPushButton *openStationButton_ = nullptr;
    QPushButton *closeStationButton_ = nullptr;
    QTableWidget *stationTable_ = nullptr;

    QSpinBox *chargerStationIdSpin_ = nullptr;
    QLineEdit *chargerCodeEdit_ = nullptr;
    QComboBox *chargerTypeCombo_ = nullptr;
    QDoubleSpinBox *chargerPowerSpin_ = nullptr;
    QComboBox *chargerStatusCombo_ = nullptr;
    QPushButton *refreshChargersButton_ = nullptr;
    QPushButton *createChargerButton_ = nullptr;
    QPushButton *updateChargerButton_ = nullptr;
    QPushButton *setChargerStatusButton_ = nullptr;
    QTableWidget *chargerTable_ = nullptr;
    QTableWidget *telemetryTable_ = nullptr;

    QSpinBox *orderUserIdSpin_ = nullptr;
    QComboBox *orderStatusCombo_ = nullptr;
    QPushButton *refreshOrdersButton_ = nullptr;
    QPushButton *stopOrderButton_ = nullptr;
    QPushButton *settleOrderButton_ = nullptr;
    QDoubleSpinBox *orderEnergySpin_ = nullptr;
    QTableWidget *orderTable_ = nullptr;

    QSpinBox *overviewStationIdSpin_ = nullptr;
    QComboBox *overviewHorizonCombo_ = nullptr;
    QPushButton *refreshOverviewButton_ = nullptr;
    QPushButton *generateForecastButton_ = nullptr;
    QPushButton *refreshForecastsButton_ = nullptr;
    QLabel *summaryOrderCountLabel_ = nullptr;
    QLabel *summaryEnergyLabel_ = nullptr;
    QLabel *summaryRevenueLabel_ = nullptr;
    QLabel *summaryAverageEnergyLabel_ = nullptr;
    QLabel *summaryLoadLabel_ = nullptr;
    QLabel *summaryUsersLabel_ = nullptr;
    QLabel *summaryStationsLabel_ = nullptr;
    QLabel *summaryChargersLabel_ = nullptr;
    QTableWidget *forecastTable_ = nullptr;
    QTableWidget *orderStatusTable_ = nullptr;
    QTableWidget *chargerStatusOverviewTable_ = nullptr;

    QString sessionToken_;
    int requestId_ = 0;
#ifdef HAS_QT_WEBENGINE
    QWebEngineView *dashboardWebView_ = nullptr;
#endif
    Ui::AdminWindow *ui = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    AdminWindow window;
    window.show();
    return app.exec();
}
