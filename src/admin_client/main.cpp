#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
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
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

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

} // namespace

class AdminWindow final : public QWidget {
    Q_OBJECT

public:
    explicit AdminWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(QStringLiteral("Charging Admin Client"));
        resize(1240, 780);
        buildUi();
        connectSignals();
    }

private:
    void buildUi()
    {
        hostEdit_ = new QLineEdit(QStringLiteral("127.0.0.1"));
        portSpin_ = new QSpinBox;
        portSpin_->setRange(1, 65535);
        portSpin_->setValue(45454);
        usernameEdit_ = new QLineEdit(QStringLiteral("admin"));
        passwordEdit_ = new QLineEdit(QStringLiteral("admin123"));
        passwordEdit_->setEchoMode(QLineEdit::Password);
        loginButton_ = new QPushButton(QStringLiteral("Login"));
        adminLabel_ = new QLabel(QStringLiteral("Not logged in"));

        auto *loginBox = new QGroupBox(QStringLiteral("Admin Login"));
        auto *loginLayout = new QFormLayout(loginBox);
        loginLayout->addRow(QStringLiteral("Host"), hostEdit_);
        loginLayout->addRow(QStringLiteral("Port"), portSpin_);
        loginLayout->addRow(QStringLiteral("Username"), usernameEdit_);
        loginLayout->addRow(QStringLiteral("Password"), passwordEdit_);
        loginLayout->addRow(loginButton_);
        loginLayout->addRow(QStringLiteral("Current Admin"), adminLabel_);

        tabs_ = new QTabWidget;
        tabs_->addTab(buildUsersTab(), QStringLiteral("Users"));
        tabs_->addTab(buildStationsTab(), QStringLiteral("Stations"));
        tabs_->addTab(buildChargersTab(), QStringLiteral("Chargers"));
        tabs_->addTab(buildOrdersTab(), QStringLiteral("Orders"));

        logEdit_ = new QPlainTextEdit;
        logEdit_->setReadOnly(true);
        logEdit_->setMaximumBlockCount(400);

        auto *root = new QVBoxLayout(this);
        root->addWidget(loginBox);
        root->addWidget(tabs_, 1);
        root->addWidget(logEdit_);
    }

    QWidget *buildUsersTab()
    {
        userKeywordEdit_ = new QLineEdit;
        userStatusCombo_ = new QComboBox;
        userStatusCombo_->addItems({QStringLiteral("all"), QStringLiteral("active"), QStringLiteral("frozen")});
        refreshUsersButton_ = new QPushButton(QStringLiteral("Refresh"));
        freezeUserButton_ = new QPushButton(QStringLiteral("Freeze"));
        activateUserButton_ = new QPushButton(QStringLiteral("Activate"));

        userTable_ = new QTableWidget(0, 5);
        userTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Phone"),
                                               QStringLiteral("Nickname"), QStringLiteral("Balance"),
                                               QStringLiteral("Status")});
        setupTable(userTable_);

        auto *tools = new QHBoxLayout;
        tools->addWidget(new QLabel(QStringLiteral("Keyword")));
        tools->addWidget(userKeywordEdit_);
        tools->addWidget(userStatusCombo_);
        tools->addWidget(refreshUsersButton_);
        tools->addWidget(freezeUserButton_);
        tools->addWidget(activateUserButton_);

        auto *tab = new QWidget;
        auto *layout = new QVBoxLayout(tab);
        layout->addLayout(tools);
        layout->addWidget(userTable_);
        return tab;
    }

    QWidget *buildStationsTab()
    {
        stationNameEdit_ = new QLineEdit;
        stationAddressEdit_ = new QLineEdit;
        stationLatSpin_ = new QDoubleSpinBox;
        stationLatSpin_->setRange(-90.0, 90.0);
        stationLatSpin_->setDecimals(6);
        stationLatSpin_->setValue(39.9);
        stationLngSpin_ = new QDoubleSpinBox;
        stationLngSpin_->setRange(-180.0, 180.0);
        stationLngSpin_->setDecimals(6);
        stationLngSpin_->setValue(116.3);
        stationStatusCombo_ = new QComboBox;
        stationStatusCombo_->addItems({QStringLiteral("open"), QStringLiteral("closed")});
        refreshStationsButton_ = new QPushButton(QStringLiteral("Refresh"));
        createStationButton_ = new QPushButton(QStringLiteral("Create"));
        updateStationButton_ = new QPushButton(QStringLiteral("Update"));
        openStationButton_ = new QPushButton(QStringLiteral("Open"));
        closeStationButton_ = new QPushButton(QStringLiteral("Close"));

        stationTable_ = new QTableWidget(0, 4);
        stationTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Name"),
                                                  QStringLiteral("Address"), QStringLiteral("Status")});
        setupTable(stationTable_);

        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("Name"), stationNameEdit_);
        form->addRow(QStringLiteral("Address"), stationAddressEdit_);
        form->addRow(QStringLiteral("Latitude"), stationLatSpin_);
        form->addRow(QStringLiteral("Longitude"), stationLngSpin_);
        form->addRow(QStringLiteral("Status"), stationStatusCombo_);

        auto *buttons = new QHBoxLayout;
        buttons->addWidget(refreshStationsButton_);
        buttons->addWidget(createStationButton_);
        buttons->addWidget(updateStationButton_);
        buttons->addWidget(openStationButton_);
        buttons->addWidget(closeStationButton_);

        auto *left = new QWidget;
        auto *leftLayout = new QVBoxLayout(left);
        leftLayout->addLayout(form);
        leftLayout->addLayout(buttons);

        auto *splitter = new QSplitter;
        splitter->addWidget(left);
        splitter->addWidget(stationTable_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);

        auto *tab = new QWidget;
        auto *layout = new QVBoxLayout(tab);
        layout->addWidget(splitter);
        return tab;
    }

    QWidget *buildChargersTab()
    {
        chargerStationIdSpin_ = new QSpinBox;
        chargerStationIdSpin_->setRange(1, 1000000);
        chargerStationIdSpin_->setValue(1);
        chargerCodeEdit_ = new QLineEdit(QStringLiteral("NEW-F-001"));
        chargerTypeCombo_ = new QComboBox;
        chargerTypeCombo_->addItems({QStringLiteral("fast"), QStringLiteral("slow")});
        chargerPowerSpin_ = new QDoubleSpinBox;
        chargerPowerSpin_->setRange(0.1, 1000.0);
        chargerPowerSpin_->setDecimals(2);
        chargerPowerSpin_->setValue(120.0);
        chargerStatusCombo_ = new QComboBox;
        chargerStatusCombo_->addItems({QStringLiteral("idle"), QStringLiteral("charging"),
                                       QStringLiteral("fault"), QStringLiteral("offline")});
        refreshChargersButton_ = new QPushButton(QStringLiteral("Refresh"));
        createChargerButton_ = new QPushButton(QStringLiteral("Create"));
        updateChargerButton_ = new QPushButton(QStringLiteral("Update"));
        setChargerStatusButton_ = new QPushButton(QStringLiteral("Set Status"));

        chargerTable_ = new QTableWidget(0, 7);
        chargerTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Station"),
                                                  QStringLiteral("Code"), QStringLiteral("Type"),
                                                  QStringLiteral("Power"), QStringLiteral("Status"),
                                                  QStringLiteral("Orders")});
        setupTable(chargerTable_);

        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("Station ID"), chargerStationIdSpin_);
        form->addRow(QStringLiteral("Code"), chargerCodeEdit_);
        form->addRow(QStringLiteral("Type"), chargerTypeCombo_);
        form->addRow(QStringLiteral("Power kW"), chargerPowerSpin_);
        form->addRow(QStringLiteral("Status"), chargerStatusCombo_);

        auto *buttons = new QHBoxLayout;
        buttons->addWidget(refreshChargersButton_);
        buttons->addWidget(createChargerButton_);
        buttons->addWidget(updateChargerButton_);
        buttons->addWidget(setChargerStatusButton_);

        telemetryTable_ = new QTableWidget(0, 5);
        telemetryTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("Status"),
                                                    QStringLiteral("Power"), QStringLiteral("Energy"),
                                                    QStringLiteral("Time")});
        setupTable(telemetryTable_);

        auto *left = new QWidget;
        auto *leftLayout = new QVBoxLayout(left);
        leftLayout->addLayout(form);
        leftLayout->addLayout(buttons);
        leftLayout->addWidget(new QLabel(QStringLiteral("Telemetry")));
        leftLayout->addWidget(telemetryTable_);

        auto *splitter = new QSplitter;
        splitter->addWidget(left);
        splitter->addWidget(chargerTable_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 2);

        auto *tab = new QWidget;
        auto *layout = new QVBoxLayout(tab);
        layout->addWidget(splitter);
        return tab;
    }

    QWidget *buildOrdersTab()
    {
        orderUserIdSpin_ = new QSpinBox;
        orderUserIdSpin_->setRange(0, 1000000);
        orderStatusCombo_ = new QComboBox;
        orderStatusCombo_->addItems({QStringLiteral("all"), QStringLiteral("reserved"),
                                     QStringLiteral("charging"), QStringLiteral("pending_settlement"),
                                     QStringLiteral("completed"), QStringLiteral("cancelled")});
        refreshOrdersButton_ = new QPushButton(QStringLiteral("Refresh Orders"));
        stopOrderButton_ = new QPushButton(QStringLiteral("Stop Charging"));
        settleOrderButton_ = new QPushButton(QStringLiteral("Settle"));
        orderEnergySpin_ = new QDoubleSpinBox;
        orderEnergySpin_->setRange(0.1, 9999.0);
        orderEnergySpin_->setDecimals(2);
        orderEnergySpin_->setValue(5.0);

        orderTable_ = new QTableWidget(0, 8);
        orderTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("No"),
                                                QStringLiteral("User"), QStringLiteral("Station"),
                                                QStringLiteral("Charger"), QStringLiteral("Status"),
                                                QStringLiteral("Energy"), QStringLiteral("Amount")});
        setupTable(orderTable_);

        auto *tools = new QHBoxLayout;
        tools->addWidget(new QLabel(QStringLiteral("User ID 0=all")));
        tools->addWidget(orderUserIdSpin_);
        tools->addWidget(orderStatusCombo_);
        tools->addWidget(refreshOrdersButton_);
        tools->addWidget(new QLabel(QStringLiteral("Stop Energy")));
        tools->addWidget(orderEnergySpin_);
        tools->addWidget(stopOrderButton_);
        tools->addWidget(settleOrderButton_);

        auto *tab = new QWidget;
        auto *layout = new QVBoxLayout(tab);
        layout->addLayout(tools);
        layout->addWidget(orderTable_);
        return tab;
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

    QString sessionToken_;
    int requestId_ = 0;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    AdminWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
