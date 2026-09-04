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

void setItem(QTableWidget *table, int row, int column, const QString &text, int id = 0)
{
    auto *item = new QTableWidgetItem(text);
    if (id > 0) {
        item->setData(Qt::UserRole, id);
    }
    table->setItem(row, column, item);
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
    Q_OBJECT

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

        setWindowTitle(QStringLiteral("Charging Admin Client"));
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
        dashboardStatusLabel_->setText(QStringLiteral("Embedded WebEngine dashboard ready"));
#else
        dashboardWebContainer_->hide();
        dashboardHtmlEdit_->setMinimumHeight(500);
        dashboardStatusLabel_->setText(QStringLiteral("HTML preview mode (Qt WebEngine unavailable)"));
#endif
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
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
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
            appendLog(QStringLiteral("[ERROR] %1").arg(message));
        }
    }

    bool ensureLogin()
    {
        if (sessionToken_.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Login Required"), QStringLiteral("Please login first."));
            return false;
        }
        return true;
    }

    void login()
    {
        runAction(QStringLiteral("Login Failed"), [this]() {
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
        runAction(QStringLiteral("Users Failed"), [this]() {
            QJsonObject payload = authed(QStringLiteral("admin.user.list"));
            const QString keyword = userKeywordEdit_->text().trimmed();
            if (!keyword.isEmpty()) {
                payload.insert(QStringLiteral("keyword"), keyword);
            }
            if (userStatusCombo_->currentText() != QStringLiteral("all")) {
                payload.insert(QStringLiteral("status"), userStatusCombo_->currentText());
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
                setItem(userTable_, row, 4, user.value(QStringLiteral("status")).toString(), id);
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
            QMessageBox::information(this, QStringLiteral("User"), QStringLiteral("Select a user first."));
            return;
        }
        runAction(QStringLiteral("User Status Failed"), [this, userId, status]() {
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
        runAction(QStringLiteral("Stations Failed"), [this]() {
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
                setItem(stationTable_, row, 3, station.value(QStringLiteral("status")).toString(), id);
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
        stationStatusCombo_->setCurrentText(currentText(stationTable_, 3));
        chargerStationIdSpin_->setValue(stationId);
    }

    QJsonObject stationPayload(const QString &action)
    {
        QJsonObject payload = authed(action);
        payload.insert(QStringLiteral("name"), stationNameEdit_->text().trimmed());
        payload.insert(QStringLiteral("address"), stationAddressEdit_->text().trimmed());
        payload.insert(QStringLiteral("latitude"), stationLatSpin_->value());
        payload.insert(QStringLiteral("longitude"), stationLngSpin_->value());
        payload.insert(QStringLiteral("status"), stationStatusCombo_->currentText());
        return payload;
    }

    void createStation()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Create Station Failed"), [this]() {
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
            QMessageBox::information(this, QStringLiteral("Station"), QStringLiteral("Select a station first."));
            return;
        }
        runAction(QStringLiteral("Update Station Failed"), [this, stationId]() {
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
            QMessageBox::information(this, QStringLiteral("Station"), QStringLiteral("Select a station first."));
            return;
        }
        runAction(QStringLiteral("Station Status Failed"), [this, stationId, status]() {
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
        runAction(QStringLiteral("Chargers Failed"), [this]() {
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
                setItem(chargerTable_, row, 3, charger.value(QStringLiteral("type")).toString(), id);
                setItem(chargerTable_, row, 4, QString::number(charger.value(QStringLiteral("power_kw")).toDouble(), 'f', 2), id);
                setItem(chargerTable_, row, 5, charger.value(QStringLiteral("status")).toString(), id);
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
        chargerTypeCombo_->setCurrentText(currentText(chargerTable_, 3));
        chargerPowerSpin_->setValue(currentText(chargerTable_, 4).toDouble());
        chargerStatusCombo_->setCurrentText(currentText(chargerTable_, 5));
        refreshTelemetry(chargerId);
    }

    QJsonObject chargerPayload(const QString &action)
    {
        QJsonObject payload = authed(action);
        payload.insert(QStringLiteral("station_id"), chargerStationIdSpin_->value());
        payload.insert(QStringLiteral("code"), chargerCodeEdit_->text().trimmed());
        payload.insert(QStringLiteral("type"), chargerTypeCombo_->currentText());
        payload.insert(QStringLiteral("power_kw"), chargerPowerSpin_->value());
        payload.insert(QStringLiteral("status"), chargerStatusCombo_->currentText());
        return payload;
    }

    void createCharger()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Create Charger Failed"), [this]() {
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
        runAction(QStringLiteral("Update Charger Failed"), [this, chargerId]() {
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
        runAction(QStringLiteral("Charger Status Failed"), [this, chargerId]() {
            QJsonObject payload = authed(QStringLiteral("admin.charger.set_status"));
            payload.insert(QStringLiteral("charger_id"), chargerId);
            payload.insert(QStringLiteral("status"), chargerStatusCombo_->currentText());
            request(payload);
            refreshChargers();
        });
    }

    void refreshTelemetry(int chargerId)
    {
        runAction(QStringLiteral("Telemetry Failed"), [this, chargerId]() {
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
                setItem(telemetryTable_, row, 1, record.value(QStringLiteral("status")).toString(), id);
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
        runAction(QStringLiteral("Overview Failed"), [this]() {
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
    body { margin: 0; background: #0f172a; color: #e2e8f0; font-family: Arial, sans-serif; }
    .wrap { padding: 16px; display: grid; gap: 12px; grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .card { background: #111827; border: 1px solid #334155; border-radius: 8px; padding: 12px; }
    .title { font-size: 14px; color: #94a3b8; }
    .value { font-size: 28px; font-weight: 700; margin-top: 6px; }
    .panel { background: #0b1120; border: 1px solid #334155; border-radius: 8px; padding: 12px; min-height: 320px; }
    #chart, #loadChart { width: 100%; height: 320px; }
    .chart-fallback { box-sizing: border-box; display: flex; align-items: center; justify-content: center; height: 320px; padding: 24px; color: #fbbf24; text-align: center; }
    pre { white-space: pre-wrap; word-break: break-word; color: #cbd5e1; font-size: 12px; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card"><div class="title">Orders</div><div class="value">%1</div></div>
    <div class="card"><div class="title">Completed Energy kWh</div><div class="value">%2</div></div>
    <div class="card"><div class="title">Revenue Yuan</div><div class="value">%3</div></div>
    <div class="card"><div class="title">Current Load kW</div><div class="value">%4</div></div>
    <div class="panel" style="grid-column: span 2;">
      <div id="chart"></div>
    </div>
    <div class="panel" style="grid-column: span 2;">
      <div id="loadChart"></div>
    </div>
    <div class="panel" style="grid-column: span 4;">
      <div class="title">Dashboard Data JSON</div>
      <pre id="dashboardJson"></pre>
    </div>
  </div>
  <script>
    const overview = %5;
    const forecasts = %6;
    const actualLoads = %7;
    document.getElementById('dashboardJson').textContent = JSON.stringify(
      { overview, actual_load_history: actualLoads, forecasts }, null, 2);
    const orderStatuses = overview.order_statuses || [];
    const chartMessage = (id, message) => {
      const element = document.getElementById(id);
      element.className = 'chart-fallback';
      element.textContent = message;
    };
    if (!window.echarts) {
      chartMessage('chart', 'ECharts could not be loaded. Connect to the network, then refresh this dashboard.');
      chartMessage('loadChart', 'Actual telemetry and forecast data are still available in the exported HTML below.');
    } else {
      const orderChart = echarts.init(document.getElementById('chart'));
      orderChart.setOption({
        backgroundColor: 'transparent',
        title: { text: 'Order Status', textStyle: { color: '#e2e8f0' } },
        tooltip: {},
        xAxis: { type: 'category', data: orderStatuses.map(x => x.status), axisLabel: { color: '#cbd5e1' } },
        yAxis: { type: 'value', axisLabel: { color: '#cbd5e1' } },
        series: [{ type: 'bar', data: orderStatuses.map(x => x.count), itemStyle: { color: '#38bdf8' } }]
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
        chartMessage('loadChart', 'No actual telemetry or forecast records match the current filters.');
      } else {
        const loadChart = echarts.init(document.getElementById('loadChart'));
        loadChart.setOption({
          backgroundColor: 'transparent',
          title: { text: 'Actual and Forecast Load', textStyle: { color: '#e2e8f0' } },
          tooltip: { trigger: 'axis' },
          legend: { data: ['Actual kW', 'Forecast kW'], textStyle: { color: '#cbd5e1' } },
          xAxis: {
            type: 'category',
            data: timeline,
            axisLabel: { color: '#cbd5e1', rotate: 25, formatter: value => value.replace('T', ' ').slice(5, 16) }
          },
          yAxis: { type: 'value', axisLabel: { color: '#cbd5e1' } },
          series: [
            {
              name: 'Actual kW',
              type: 'line',
              smooth: true,
              connectNulls: false,
              data: timeline.map(time => actualByTime.has(time) ? actualByTime.get(time) : null),
              itemStyle: { color: '#38bdf8' }
            },
            {
              name: 'Forecast kW',
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
        const QString horizonText = overviewHorizonCombo_->currentText();
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
        dashboardStatusLabel_->setText(QStringLiteral("Dashboard updated"));
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
        runAction(QStringLiteral("Dashboard Failed"), [this]() {
            const QJsonObject overview = dashboardOverview();
            const QJsonArray forecasts = dashboardForecasts();
            const QJsonArray actualLoads = dashboardLoadHistory();
            renderDashboard(overview, forecasts, actualLoads);
        });
    }

    void exportDashboardHtml()
    {
        if (dashboardHtmlEdit_->toPlainText().isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Dashboard"), QStringLiteral("Refresh dashboard first."));
            return;
        }
        const QString filePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export Dashboard HTML"), QString(), QStringLiteral("HTML Files (*.html)"));
        if (filePath.isEmpty()) {
            return;
        }
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("Export Failed"), file.errorString());
            return;
        }
        QTextStream stream(&file);
        stream << dashboardHtmlEdit_->toPlainText();
        dashboardStatusLabel_->setText(QStringLiteral("Dashboard exported"));
    }

    void generateForecast()
    {
        if (!ensureLogin()) {
            return;
        }
        runAction(QStringLiteral("Generate Forecast Failed"), [this]() {
            QJsonObject payload = authed(QStringLiteral("forecast.generate"));
            if (overviewStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
            }
            const QString horizonText = overviewHorizonCombo_->currentText();
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
        runAction(QStringLiteral("Forecasts Failed"), [this]() {
            QJsonObject payload = authed(QStringLiteral("forecast.list"));
            if (overviewStationIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("station_id"), overviewStationIdSpin_->value());
            }
            const QString horizonText = overviewHorizonCombo_->currentText();
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
            setItem(table, row, 0, item.value(QStringLiteral("status")).toString());
            setItem(table, row, 1, QString::number(item.value(countKey).toInt()));
        }
        table->resizeColumnsToContents();
    }

    void fillChargerStatusTable(QTableWidget *table, const QJsonArray &rows)
    {
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row) {
            const QJsonObject item = rows.at(row).toObject();
            setItem(table, row, 0, item.value(QStringLiteral("status")).toString());
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
        runAction(QStringLiteral("Orders Failed"), [this]() {
            QJsonObject payload = authed(QStringLiteral("order.list"));
            if (orderUserIdSpin_->value() > 0) {
                payload.insert(QStringLiteral("user_id"), orderUserIdSpin_->value());
            }
            if (orderStatusCombo_->currentText() != QStringLiteral("all")) {
                payload.insert(QStringLiteral("status"), orderStatusCombo_->currentText());
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
                setItem(orderTable_, row, 5, order.value(QStringLiteral("status")).toString(), id);
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
        runAction(QStringLiteral("Stop Order Failed"), [this, orderId]() {
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
        runAction(QStringLiteral("Settle Order Failed"), [this, orderId]() {
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

#include "main.moc"
