#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kDefaultPort = 45454;
constexpr int kEnergyPriceCentsPerKwh = 120;
const QString kConnectionName = QStringLiteral("charging_server_db");

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QJsonObject okResponse(const QString &action, const QJsonObject &data = {})
{
    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("action"), action},
        {QStringLiteral("server_time"), nowUtc()},
        {QStringLiteral("data"), data},
    };
}

QJsonObject errorResponse(const QString &action, const QString &message,
                          const QString &errorCode = QStringLiteral("REQUEST_FAILED"))
{
    return {
        {QStringLiteral("success"), false},
        {QStringLiteral("action"), action},
        {QStringLiteral("server_time"), nowUtc()},
        {QStringLiteral("error_code"), errorCode},
        {QStringLiteral("error"), message},
    };
}

QByteArray toLine(const QJsonObject &response, const QJsonValue &requestId = {})
{
    QJsonObject copy = response;
    if (!requestId.isUndefined()) {
        copy.insert(QStringLiteral("request_id"), requestId);
    }
    return QJsonDocument(copy).toJson(QJsonDocument::Compact) + '\n';
}

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QStringList splitSqlStatements(const QString &script)
{
    QStringList statements;
    QString current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (int i = 0; i < script.size(); ++i) {
        const QChar ch = script.at(i);
        const QChar prev = i > 0 ? script.at(i - 1) : QChar();

        if (ch == u'\'' && !inDoubleQuote && prev != u'\\') {
            inSingleQuote = !inSingleQuote;
        } else if (ch == u'"' && !inSingleQuote && prev != u'\\') {
            inDoubleQuote = !inDoubleQuote;
        }

        if (ch == u';' && !inSingleQuote && !inDoubleQuote) {
            const QString trimmed = current.trimmed();
            if (!trimmed.isEmpty()) {
                statements.append(trimmed);
            }
            current.clear();
            continue;
        }

        current.append(ch);
    }

    const QString trimmed = current.trimmed();
    if (!trimmed.isEmpty()) {
        statements.append(trimmed);
    }
    return statements;
}

bool executeScript(QSqlDatabase &db, const QString &path, QString *error)
{
    const QString script = readTextFile(path);
    if (script.isEmpty()) {
        *error = QStringLiteral("SQL script is empty or unreadable: %1").arg(path);
        return false;
    }

    for (const QString &statement : splitSqlStatements(script)) {
        QSqlQuery query(db);
        if (!query.exec(statement)) {
            *error = QStringLiteral("SQL failed in %1: %2").arg(path, query.lastError().text());
            return false;
        }
    }
    return true;
}

bool cleanupExpiredSessions(QSqlDatabase &db, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM auth_sessions WHERE expires_at <= :now"));
    query.bindValue(QStringLiteral(":now"), nowUtc());
    if (!query.exec()) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

QString sha256Hex(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariant sqlValue(const QSqlQuery &query, const QString &fieldName)
{
    return query.value(query.record().indexOf(fieldName));
}

QJsonObject userToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("phone"), sqlValue(query, QStringLiteral("phone")).toString()},
        {QStringLiteral("nickname"), sqlValue(query, QStringLiteral("nickname")).toString()},
        {QStringLiteral("avatar_url"), sqlValue(query, QStringLiteral("avatar_url")).toString()},
        {QStringLiteral("balance_cents"), sqlValue(query, QStringLiteral("balance_cents")).toInt()},
        {QStringLiteral("balance_yuan"), sqlValue(query, QStringLiteral("balance_cents")).toInt() / 100.0},
        {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
    };
}

QJsonObject stationToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("name"), sqlValue(query, QStringLiteral("name")).toString()},
        {QStringLiteral("address"), sqlValue(query, QStringLiteral("address")).toString()},
        {QStringLiteral("latitude"), sqlValue(query, QStringLiteral("latitude")).toDouble()},
        {QStringLiteral("longitude"), sqlValue(query, QStringLiteral("longitude")).toDouble()},
        {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
    };
}

bool isValidStationStatus(const QString &status)
{
    return status == QStringLiteral("open") || status == QStringLiteral("closed");
}

bool isValidChargerType(const QString &type)
{
    return type == QStringLiteral("fast") || type == QStringLiteral("slow");
}

bool isValidChargerStatus(const QString &status)
{
    return status == QStringLiteral("idle") || status == QStringLiteral("charging")
        || status == QStringLiteral("fault") || status == QStringLiteral("offline");
}

bool isValidOrderStatus(const QString &status)
{
    return status == QStringLiteral("reserved") || status == QStringLiteral("charging")
        || status == QStringLiteral("pending_settlement") || status == QStringLiteral("completed")
        || status == QStringLiteral("cancelled");
}

QJsonObject chargerToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("station_id"), sqlValue(query, QStringLiteral("station_id")).toInt()},
        {QStringLiteral("station_name"), sqlValue(query, QStringLiteral("station_name")).toString()},
        {QStringLiteral("code"), sqlValue(query, QStringLiteral("code")).toString()},
        {QStringLiteral("type"), sqlValue(query, QStringLiteral("type")).toString()},
        {QStringLiteral("power_kw"), sqlValue(query, QStringLiteral("power_kw")).toDouble()},
        {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
        {QStringLiteral("current_power_kw"), sqlValue(query, QStringLiteral("current_power_kw")).toDouble()},
        {QStringLiteral("total_orders"), sqlValue(query, QStringLiteral("total_orders")).toInt()},
        {QStringLiteral("total_energy_kwh"), sqlValue(query, QStringLiteral("total_energy_kwh")).toDouble()},
        {QStringLiteral("total_duration_minutes"), sqlValue(query, QStringLiteral("total_duration_minutes")).toInt()},
    };
}

QJsonObject orderToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("order_no"), sqlValue(query, QStringLiteral("order_no")).toString()},
        {QStringLiteral("user_id"), sqlValue(query, QStringLiteral("user_id")).toInt()},
        {QStringLiteral("user_phone"), sqlValue(query, QStringLiteral("user_phone")).toString()},
        {QStringLiteral("charger_id"), sqlValue(query, QStringLiteral("charger_id")).toInt()},
        {QStringLiteral("charger_code"), sqlValue(query, QStringLiteral("charger_code")).toString()},
        {QStringLiteral("station_name"), sqlValue(query, QStringLiteral("station_name")).toString()},
        {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
        {QStringLiteral("reserved_at"), sqlValue(query, QStringLiteral("reserved_at")).toString()},
        {QStringLiteral("started_at"), sqlValue(query, QStringLiteral("started_at")).toString()},
        {QStringLiteral("stopped_at"), sqlValue(query, QStringLiteral("stopped_at")).toString()},
        {QStringLiteral("settled_at"), sqlValue(query, QStringLiteral("settled_at")).toString()},
        {QStringLiteral("energy_kwh"), sqlValue(query, QStringLiteral("energy_kwh")).toDouble()},
        {QStringLiteral("amount_cents"), sqlValue(query, QStringLiteral("amount_cents")).toInt()},
        {QStringLiteral("amount_yuan"), sqlValue(query, QStringLiteral("amount_cents")).toInt() / 100.0},
    };
}

QJsonObject balanceLogToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("user_id"), sqlValue(query, QStringLiteral("user_id")).toInt()},
        {QStringLiteral("user_phone"), sqlValue(query, QStringLiteral("user_phone")).toString()},
        {QStringLiteral("change_cents"), sqlValue(query, QStringLiteral("change_cents")).toInt()},
        {QStringLiteral("change_yuan"), sqlValue(query, QStringLiteral("change_cents")).toInt() / 100.0},
        {QStringLiteral("balance_after_cents"), sqlValue(query, QStringLiteral("balance_after_cents")).toInt()},
        {QStringLiteral("balance_after_yuan"), sqlValue(query, QStringLiteral("balance_after_cents")).toInt() / 100.0},
        {QStringLiteral("reason"), sqlValue(query, QStringLiteral("reason")).toString()},
        {QStringLiteral("related_order_id"), sqlValue(query, QStringLiteral("related_order_id")).toInt()},
        {QStringLiteral("created_at"), sqlValue(query, QStringLiteral("created_at")).toString()},
    };
}

QJsonObject telemetryToJson(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
        {QStringLiteral("charger_id"), sqlValue(query, QStringLiteral("charger_id")).toInt()},
        {QStringLiteral("charger_code"), sqlValue(query, QStringLiteral("charger_code")).toString()},
        {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
        {QStringLiteral("power_kw"), sqlValue(query, QStringLiteral("power_kw")).toDouble()},
        {QStringLiteral("energy_kwh"), sqlValue(query, QStringLiteral("energy_kwh")).toDouble()},
        {QStringLiteral("recorded_at"), sqlValue(query, QStringLiteral("recorded_at")).toString()},
    };
}

QString makeOrderNo()
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const int suffix = static_cast<int>(QRandomGenerator::global()->bounded(1000));
    return QStringLiteral("ORD%1%2").arg(timestamp, QStringLiteral("%1").arg(suffix, 3, 10, QChar(u'0')));
}

int requestInt(const QJsonObject &request, const QString &fieldName, int defaultValue = -1)
{
    const QJsonValue value = request.value(fieldName);
    if (value.isDouble()) {
        return value.toInt(defaultValue);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : defaultValue;
    }
    return defaultValue;
}

double requestDouble(const QJsonObject &request, const QString &fieldName, double defaultValue = -1.0)
{
    const QJsonValue value = request.value(fieldName);
    if (value.isDouble()) {
        return value.toDouble(defaultValue);
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : defaultValue;
    }
    return defaultValue;
}

QDateTime parseIsoDateTime(const QString &value)
{
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, Qt::ISODate);
    }
    return parsed;
}

int durationMinutes(const QString &startedAt, const QString &stoppedAt)
{
    const QDateTime started = parseIsoDateTime(startedAt);
    const QDateTime stopped = parseIsoDateTime(stoppedAt);
    if (!started.isValid() || !stopped.isValid()) {
        return 0;
    }
    return std::max(1, static_cast<int>(started.secsTo(stopped) / 60));
}

QString makeSessionToken(const QString &actorType, int actorId)
{
    const QString random = QString::number(QRandomGenerator::global()->generate64(), 16);
    return QStringLiteral("%1-%2-%3-%4")
        .arg(actorType, QString::number(actorId), QString::number(QDateTime::currentMSecsSinceEpoch()), random);
}

} // namespace

class ChargingServer final : public QTcpServer {
    Q_OBJECT

public:
    explicit ChargingServer(QSqlDatabase db, QObject *parent = nullptr)
        : QTcpServer(parent)
        , db_(std::move(db))
    {
        connect(this, &QTcpServer::newConnection, this, &ChargingServer::acceptConnections);
    }

private slots:
    void acceptConnections()
    {
        while (QTcpSocket *socket = nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readClient(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            qInfo().noquote() << "client connected" << socket->peerAddress().toString() << socket->peerPort();
        }
    }

    void readClient(QTcpSocket *socket)
    {
        while (socket->canReadLine()) {
            const QByteArray line = socket->readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }

            QElapsedTimer elapsed;
            elapsed.start();
            QJsonParseError parseError;
            const QJsonDocument requestDoc = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject()) {
                qWarning().noquote() << "invalid request from" << socket->peerAddress().toString()
                                     << socket->peerPort() << parseError.errorString();
                socket->write(toLine(errorResponse(QStringLiteral("unknown"),
                                                   QStringLiteral("invalid JSON request"),
                                                   QStringLiteral("INVALID_JSON"))));
                continue;
            }

            const QJsonObject request = requestDoc.object();
            const QString action = request.value(QStringLiteral("action")).toString();
            const QJsonValue requestId = request.value(QStringLiteral("request_id"));
            QJsonObject response;

            if (action == QStringLiteral("ping")) {
                response = handlePing();
            } else if (action == QStringLiteral("user.login")) {
                response = handleUserLogin(request);
            } else if (action == QStringLiteral("admin.login")) {
                response = handleAdminLogin(request);
            } else if (action == QStringLiteral("user.profile")) {
                response = handleUserProfile(request);
            } else if (action == QStringLiteral("user.update_profile")) {
                response = handleUserUpdateProfile(request);
            } else if (action == QStringLiteral("admin.user.list")) {
                response = handleAdminUserList(request);
            } else if (action == QStringLiteral("admin.user.set_status")) {
                response = handleAdminUserSetStatus(request);
            } else if (action == QStringLiteral("admin.station.create")) {
                response = handleAdminStationCreate(request);
            } else if (action == QStringLiteral("admin.station.update")) {
                response = handleAdminStationUpdate(request);
            } else if (action == QStringLiteral("admin.station.set_status")) {
                response = handleAdminStationSetStatus(request);
            } else if (action == QStringLiteral("admin.charger.list")) {
                response = handleAdminChargerList(request);
            } else if (action == QStringLiteral("admin.charger.create")) {
                response = handleAdminChargerCreate(request);
            } else if (action == QStringLiteral("admin.charger.update")) {
                response = handleAdminChargerUpdate(request);
            } else if (action == QStringLiteral("admin.charger.set_status")) {
                response = handleAdminChargerSetStatus(request);
            } else if (action == QStringLiteral("station.list")) {
                response = handleStationList(request);
            } else if (action == QStringLiteral("charger.list")) {
                response = handleChargerList(request);
            } else if (action == QStringLiteral("order.create")) {
                response = handleOrderCreate(request);
            } else if (action == QStringLiteral("order.start")) {
                response = handleOrderStart(request);
            } else if (action == QStringLiteral("order.stop")) {
                response = handleOrderStop(request);
            } else if (action == QStringLiteral("order.cancel")) {
                response = handleOrderCancel(request);
            } else if (action == QStringLiteral("order.settle")) {
                response = handleOrderSettle(request);
            } else if (action == QStringLiteral("order.current")) {
                response = handleOrderCurrent(request);
            } else if (action == QStringLiteral("order.list")) {
                response = handleOrderList(request);
            } else if (action == QStringLiteral("balance.recharge")) {
                response = handleBalanceRecharge(request);
            } else if (action == QStringLiteral("balance.logs")) {
                response = handleBalanceLogs(request);
            } else if (action == QStringLiteral("telemetry.report")) {
                response = handleTelemetryReport(request);
            } else if (action == QStringLiteral("telemetry.list")) {
                response = handleTelemetryList(request);
            } else {
                response = errorResponse(action.isEmpty() ? QStringLiteral("unknown") : action,
                                         QStringLiteral("unsupported action"),
                                         QStringLiteral("UNSUPPORTED_ACTION"));
            }

            socket->write(toLine(response, requestId));
            qInfo().noquote() << "request action=" << (action.isEmpty() ? QStringLiteral("unknown") : action)
                              << "success=" << response.value(QStringLiteral("success")).toBool()
                              << "elapsed_ms=" << elapsed.elapsed()
                              << "peer=" << socket->peerAddress().toString() << socket->peerPort();
        }
    }

private:
    QJsonObject handlePing() const
    {
        return okResponse(QStringLiteral("ping"), {
            {QStringLiteral("status"), QStringLiteral("ok")},
        });
    }

    QJsonObject handleUserLogin(const QJsonObject &request)
    {
        const QString phone = request.value(QStringLiteral("phone")).toString().trimmed();
        if (phone.isEmpty()) {
            return errorResponse(QStringLiteral("user.login"), QStringLiteral("phone is required"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, phone, nickname, avatar_url, balance_cents, status "
            "FROM users WHERE phone = :phone"));
        query.bindValue(QStringLiteral(":phone"), phone);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("user.login"), query.lastError().text());
        }
        if (!query.next()) {
            return errorResponse(QStringLiteral("user.login"), QStringLiteral("user not found"));
        }
        if (sqlValue(query, QStringLiteral("status")).toString() != QStringLiteral("active")) {
            return errorResponse(QStringLiteral("user.login"), QStringLiteral("user account is frozen"));
        }

        QString sessionError;
        const QJsonObject session = createSession(QStringLiteral("user"),
                                                  sqlValue(query, QStringLiteral("id")).toInt(),
                                                  &sessionError);
        if (session.isEmpty()) {
            return errorResponse(QStringLiteral("user.login"), sessionError);
        }

        return okResponse(QStringLiteral("user.login"), {
            {QStringLiteral("user"), userToJson(query)},
            {QStringLiteral("session"), session},
        });
    }

    QJsonObject handleAdminLogin(const QJsonObject &request)
    {
        const QString username = request.value(QStringLiteral("username")).toString().trimmed();
        const QString password = request.value(QStringLiteral("password")).toString();
        if (username.isEmpty() || password.isEmpty()) {
            return errorResponse(QStringLiteral("admin.login"), QStringLiteral("username and password are required"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, username, password_hash, password_salt, display_name, status "
            "FROM admins WHERE username = :username"));
        query.bindValue(QStringLiteral(":username"), username);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.login"), query.lastError().text());
        }
        if (!query.next()) {
            return errorResponse(QStringLiteral("admin.login"), QStringLiteral("invalid username or password"));
        }
        if (sqlValue(query, QStringLiteral("status")).toString() != QStringLiteral("active")) {
            return errorResponse(QStringLiteral("admin.login"), QStringLiteral("admin account is disabled"));
        }

        const QString salt = sqlValue(query, QStringLiteral("password_salt")).toString();
        const QString expectedHash = sqlValue(query, QStringLiteral("password_hash")).toString();
        if (sha256Hex(password + salt) != expectedHash) {
            return errorResponse(QStringLiteral("admin.login"), QStringLiteral("invalid username or password"));
        }

        QString sessionError;
        const QJsonObject session = createSession(QStringLiteral("admin"),
                                                  sqlValue(query, QStringLiteral("id")).toInt(),
                                                  &sessionError);
        if (session.isEmpty()) {
            return errorResponse(QStringLiteral("admin.login"), sessionError);
        }

        return okResponse(QStringLiteral("admin.login"), {
            {QStringLiteral("admin"), QJsonObject{
                {QStringLiteral("id"), sqlValue(query, QStringLiteral("id")).toInt()},
                {QStringLiteral("username"), sqlValue(query, QStringLiteral("username")).toString()},
                {QStringLiteral("display_name"), sqlValue(query, QStringLiteral("display_name")).toString()},
                {QStringLiteral("status"), sqlValue(query, QStringLiteral("status")).toString()},
            }},
            {QStringLiteral("session"), session},
        });
    }

    QJsonObject handleUserProfile(const QJsonObject &request)
    {
        int userId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("user"), &userId, &error)) {
            return errorResponse(QStringLiteral("user.profile"), error);
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, phone, nickname, avatar_url, balance_cents, status FROM users WHERE id = :user_id"));
        query.bindValue(QStringLiteral(":user_id"), userId);
        if (!query.exec() || !query.next()) {
            return errorResponse(QStringLiteral("user.profile"), QStringLiteral("user not found"));
        }

        return okResponse(QStringLiteral("user.profile"), {
            {QStringLiteral("user"), userToJson(query)},
        });
    }

    QJsonObject handleUserUpdateProfile(const QJsonObject &request)
    {
        int userId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("user"), &userId, &error)) {
            return errorResponse(QStringLiteral("user.update_profile"), error);
        }

        const QString nickname = request.value(QStringLiteral("nickname")).toString().trimmed();
        const QString avatarUrl = request.value(QStringLiteral("avatar_url")).toString().trimmed();
        if (nickname.isEmpty() && avatarUrl.isEmpty()) {
            return errorResponse(QStringLiteral("user.update_profile"),
                                 QStringLiteral("nickname or avatar_url is required"));
        }

        QStringList sets;
        if (!nickname.isEmpty()) {
            sets.append(QStringLiteral("nickname = :nickname"));
        }
        if (!avatarUrl.isEmpty()) {
            sets.append(QStringLiteral("avatar_url = :avatar_url"));
        }
        sets.append(QStringLiteral("updated_at = :now"));

        QSqlQuery update(db_);
        update.prepare(QStringLiteral("UPDATE users SET ") + sets.join(QStringLiteral(", "))
                       + QStringLiteral(" WHERE id = :user_id"));
        if (!nickname.isEmpty()) {
            update.bindValue(QStringLiteral(":nickname"), nickname);
        }
        if (!avatarUrl.isEmpty()) {
            update.bindValue(QStringLiteral(":avatar_url"), avatarUrl);
        }
        update.bindValue(QStringLiteral(":now"), nowUtc());
        update.bindValue(QStringLiteral(":user_id"), userId);
        if (!update.exec()) {
            return errorResponse(QStringLiteral("user.update_profile"), update.lastError().text());
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, phone, nickname, avatar_url, balance_cents, status FROM users WHERE id = :user_id"));
        query.bindValue(QStringLiteral(":user_id"), userId);
        if (!query.exec() || !query.next()) {
            return errorResponse(QStringLiteral("user.update_profile"), QStringLiteral("user not found"));
        }

        return okResponse(QStringLiteral("user.update_profile"), {
            {QStringLiteral("user"), userToJson(query)},
        });
    }

    QJsonObject handleAdminUserList(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.user.list"), error);
        }
        Q_UNUSED(adminId)

        const QString keyword = request.value(QStringLiteral("keyword")).toString().trimmed();
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        const int limit = std::clamp(requestInt(request, QStringLiteral("limit"), 100), 1, 200);

        QString sql = QStringLiteral("SELECT id, phone, nickname, avatar_url, balance_cents, status FROM users");
        QStringList clauses;
        if (!keyword.isEmpty()) {
            clauses.append(QStringLiteral("(phone LIKE :keyword OR nickname LIKE :keyword)"));
        }
        if (!status.isEmpty()) {
            if (status != QStringLiteral("active") && status != QStringLiteral("frozen")) {
                return errorResponse(QStringLiteral("admin.user.list"), QStringLiteral("invalid user status"));
            }
            clauses.append(QStringLiteral("status = :status"));
        }
        if (!clauses.isEmpty()) {
            sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
        }
        sql += QStringLiteral(" ORDER BY id ASC LIMIT :limit");

        QSqlQuery query(db_);
        query.prepare(sql);
        if (!keyword.isEmpty()) {
            query.bindValue(QStringLiteral(":keyword"), QStringLiteral("%") + keyword + QStringLiteral("%"));
        }
        if (!status.isEmpty()) {
            query.bindValue(QStringLiteral(":status"), status);
        }
        query.bindValue(QStringLiteral(":limit"), limit);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.user.list"), query.lastError().text());
        }

        QJsonArray users;
        while (query.next()) {
            users.append(userToJson(query));
        }
        return okResponse(QStringLiteral("admin.user.list"), {
            {QStringLiteral("users"), users},
        });
    }

    QJsonObject handleAdminUserSetStatus(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.user.set_status"), error);
        }
        Q_UNUSED(adminId)

        const int userId = requestInt(request, QStringLiteral("user_id"));
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        if (userId <= 0 || (status != QStringLiteral("active") && status != QStringLiteral("frozen"))) {
            return errorResponse(QStringLiteral("admin.user.set_status"),
                                 QStringLiteral("user_id and valid status are required"));
        }

        QSqlQuery exists(db_);
        exists.prepare(QStringLiteral("SELECT COUNT(*) AS count FROM users WHERE id = :user_id"));
        exists.bindValue(QStringLiteral(":user_id"), userId);
        if (!exists.exec() || !exists.next()) {
            return errorResponse(QStringLiteral("admin.user.set_status"), exists.lastError().text());
        }
        if (sqlValue(exists, QStringLiteral("count")).toInt() == 0) {
            return errorResponse(QStringLiteral("admin.user.set_status"), QStringLiteral("user not found"));
        }

        QSqlQuery update(db_);
        update.prepare(QStringLiteral("UPDATE users SET status = :status, updated_at = :now WHERE id = :user_id"));
        update.bindValue(QStringLiteral(":status"), status);
        update.bindValue(QStringLiteral(":now"), nowUtc());
        update.bindValue(QStringLiteral(":user_id"), userId);
        if (!update.exec()) {
            return errorResponse(QStringLiteral("admin.user.set_status"), update.lastError().text());
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, phone, nickname, avatar_url, balance_cents, status FROM users WHERE id = :user_id"));
        query.bindValue(QStringLiteral(":user_id"), userId);
        if (!query.exec() || !query.next()) {
            return errorResponse(QStringLiteral("admin.user.set_status"), QStringLiteral("user not found"));
        }

        return okResponse(QStringLiteral("admin.user.set_status"), {
            {QStringLiteral("user"), userToJson(query)},
        });
    }

    QJsonObject handleAdminStationCreate(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.station.create"), error);
        }
        Q_UNUSED(adminId)

        const QString name = request.value(QStringLiteral("name")).toString().trimmed();
        const QString address = request.value(QStringLiteral("address")).toString().trimmed();
        const double latitude = requestDouble(request, QStringLiteral("latitude"), 999.0);
        const double longitude = requestDouble(request, QStringLiteral("longitude"), 999.0);
        const QString status = request.value(QStringLiteral("status")).toString(QStringLiteral("open")).trimmed();

        if (name.isEmpty() || address.isEmpty() || latitude < -90.0 || latitude > 90.0
            || longitude < -180.0 || longitude > 180.0 || !isValidStationStatus(status)) {
            return errorResponse(QStringLiteral("admin.station.create"),
                                 QStringLiteral("name, address, valid latitude, longitude and status are required"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "INSERT INTO stations (name, address, latitude, longitude, status, created_at, updated_at) "
            "VALUES (:name, :address, :latitude, :longitude, :status, :now, :now)"));
        bindStationValues(query, name, address, latitude, longitude, status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.station.create"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.station.create"), {
            {QStringLiteral("station"), fetchStation(query.lastInsertId().toInt())},
        });
    }

    QJsonObject handleAdminStationUpdate(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.station.update"), error);
        }
        Q_UNUSED(adminId)

        const int stationId = requestInt(request, QStringLiteral("station_id"));
        const QString name = request.value(QStringLiteral("name")).toString().trimmed();
        const QString address = request.value(QStringLiteral("address")).toString().trimmed();
        const double latitude = requestDouble(request, QStringLiteral("latitude"), 999.0);
        const double longitude = requestDouble(request, QStringLiteral("longitude"), 999.0);
        const QString status = request.value(QStringLiteral("status")).toString(QStringLiteral("open")).trimmed();

        if (stationId <= 0 || name.isEmpty() || address.isEmpty() || latitude < -90.0 || latitude > 90.0
            || longitude < -180.0 || longitude > 180.0 || !isValidStationStatus(status)) {
            return errorResponse(QStringLiteral("admin.station.update"),
                                 QStringLiteral("station_id, name, address, valid latitude, longitude and status are required"));
        }
        if (fetchStation(stationId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.station.update"), QStringLiteral("station not found"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "UPDATE stations "
            "SET name = :name, address = :address, latitude = :latitude, longitude = :longitude, "
            "    status = :status, updated_at = :now "
            "WHERE id = :station_id"));
        bindStationValues(query, name, address, latitude, longitude, status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.station.update"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.station.update"), {
            {QStringLiteral("station"), fetchStation(stationId)},
        });
    }

    QJsonObject handleAdminStationSetStatus(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.station.set_status"), error);
        }
        Q_UNUSED(adminId)

        const int stationId = requestInt(request, QStringLiteral("station_id"));
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        if (stationId <= 0 || !isValidStationStatus(status)) {
            return errorResponse(QStringLiteral("admin.station.set_status"),
                                 QStringLiteral("station_id and valid status are required"));
        }
        if (fetchStation(stationId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.station.set_status"), QStringLiteral("station not found"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral("UPDATE stations SET status = :status, updated_at = :now WHERE id = :station_id"));
        query.bindValue(QStringLiteral(":status"), status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.station.set_status"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.station.set_status"), {
            {QStringLiteral("station"), fetchStation(stationId)},
        });
    }

    QJsonObject handleAdminChargerList(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.charger.list"), error);
        }
        Q_UNUSED(adminId)

        const int stationId = requestInt(request, QStringLiteral("station_id"), 0);
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        const int limit = std::clamp(requestInt(request, QStringLiteral("limit"), 200), 1, 500);

        if (!status.isEmpty() && !isValidChargerStatus(status)) {
            return errorResponse(QStringLiteral("admin.charger.list"), QStringLiteral("invalid charger status"));
        }

        QString sql = chargerSelectSql();
        QStringList clauses;
        if (stationId > 0) {
            clauses.append(QStringLiteral("c.station_id = :station_id"));
        }
        if (!status.isEmpty()) {
            clauses.append(QStringLiteral("c.status = :status"));
        }
        if (!clauses.isEmpty()) {
            sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
        }
        sql += QStringLiteral(" ORDER BY c.id ASC LIMIT :limit");

        QSqlQuery query(db_);
        query.prepare(sql);
        if (stationId > 0) {
            query.bindValue(QStringLiteral(":station_id"), stationId);
        }
        if (!status.isEmpty()) {
            query.bindValue(QStringLiteral(":status"), status);
        }
        query.bindValue(QStringLiteral(":limit"), limit);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.charger.list"), query.lastError().text());
        }

        QJsonArray chargers;
        while (query.next()) {
            chargers.append(chargerToJson(query));
        }
        return okResponse(QStringLiteral("admin.charger.list"), {
            {QStringLiteral("chargers"), chargers},
        });
    }

    QJsonObject handleAdminChargerCreate(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.charger.create"), error);
        }
        Q_UNUSED(adminId)

        const int stationId = requestInt(request, QStringLiteral("station_id"));
        const QString code = request.value(QStringLiteral("code")).toString().trimmed();
        const QString type = request.value(QStringLiteral("type")).toString().trimmed();
        const double powerKw = requestDouble(request, QStringLiteral("power_kw"), -1.0);
        const QString status = request.value(QStringLiteral("status")).toString(QStringLiteral("idle")).trimmed();

        if (stationId <= 0 || code.isEmpty() || !isValidChargerType(type) || powerKw <= 0
            || !isValidChargerStatus(status) || status == QStringLiteral("charging")) {
            return errorResponse(QStringLiteral("admin.charger.create"),
                                 QStringLiteral("station_id, code, type, positive power_kw and non-charging status are required"));
        }
        if (fetchStation(stationId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.charger.create"), QStringLiteral("station not found"));
        }
        if (chargerCodeExists(code)) {
            return errorResponse(QStringLiteral("admin.charger.create"), QStringLiteral("charger code already exists"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "INSERT INTO chargers (station_id, code, type, power_kw, status, current_power_kw, created_at, updated_at) "
            "VALUES (:station_id, :code, :type, :power_kw, :status, 0, :now, :now)"));
        bindChargerValues(query, stationId, code, type, powerKw, status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.charger.create"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.charger.create"), {
            {QStringLiteral("charger"), fetchCharger(query.lastInsertId().toInt())},
        });
    }

    QJsonObject handleAdminChargerUpdate(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.charger.update"), error);
        }
        Q_UNUSED(adminId)

        const int chargerId = requestInt(request, QStringLiteral("charger_id"));
        const int stationId = requestInt(request, QStringLiteral("station_id"));
        const QString code = request.value(QStringLiteral("code")).toString().trimmed();
        const QString type = request.value(QStringLiteral("type")).toString().trimmed();
        const double powerKw = requestDouble(request, QStringLiteral("power_kw"), -1.0);
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();

        if (chargerId <= 0 || stationId <= 0 || code.isEmpty() || !isValidChargerType(type)
            || powerKw <= 0 || !isValidChargerStatus(status)) {
            return errorResponse(QStringLiteral("admin.charger.update"),
                                 QStringLiteral("charger_id, station_id, code, type, positive power_kw and status are required"));
        }
        if (fetchCharger(chargerId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.charger.update"), QStringLiteral("charger not found"));
        }
        if (fetchStation(stationId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.charger.update"), QStringLiteral("station not found"));
        }
        if (chargerCodeExists(code, chargerId)) {
            return errorResponse(QStringLiteral("admin.charger.update"), QStringLiteral("charger code already exists"));
        }
        if (chargerHasActiveOrder(chargerId) && status != QStringLiteral("charging")) {
            return errorResponse(QStringLiteral("admin.charger.update"),
                                 QStringLiteral("charger has active order and cannot be set to this status"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "UPDATE chargers "
            "SET station_id = :station_id, code = :code, type = :type, power_kw = :power_kw, "
            "    status = :status, current_power_kw = CASE WHEN :status = 'charging' THEN current_power_kw ELSE 0 END, "
            "    updated_at = :now "
            "WHERE id = :charger_id"));
        bindChargerValues(query, stationId, code, type, powerKw, status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.charger.update"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.charger.update"), {
            {QStringLiteral("charger"), fetchCharger(chargerId)},
        });
    }

    QJsonObject handleAdminChargerSetStatus(const QJsonObject &request)
    {
        int adminId = 0;
        QString error;
        if (!requireSession(request, QStringLiteral("admin"), &adminId, &error)) {
            return errorResponse(QStringLiteral("admin.charger.set_status"), error);
        }
        Q_UNUSED(adminId)

        const int chargerId = requestInt(request, QStringLiteral("charger_id"));
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        if (chargerId <= 0 || !isValidChargerStatus(status)) {
            return errorResponse(QStringLiteral("admin.charger.set_status"),
                                 QStringLiteral("charger_id and valid status are required"));
        }
        if (fetchCharger(chargerId).isEmpty()) {
            return errorResponse(QStringLiteral("admin.charger.set_status"), QStringLiteral("charger not found"));
        }
        if (chargerHasActiveOrder(chargerId) && status != QStringLiteral("charging")) {
            return errorResponse(QStringLiteral("admin.charger.set_status"),
                                 QStringLiteral("charger has active order and cannot be set to this status"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "UPDATE chargers "
            "SET status = :status, current_power_kw = CASE WHEN :status = 'charging' THEN current_power_kw ELSE 0 END, "
            "    updated_at = :now "
            "WHERE id = :charger_id"));
        query.bindValue(QStringLiteral(":status"), status);
        query.bindValue(QStringLiteral(":now"), nowUtc());
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("admin.charger.set_status"), query.lastError().text());
        }

        return okResponse(QStringLiteral("admin.charger.set_status"), {
            {QStringLiteral("charger"), fetchCharger(chargerId)},
        });
    }

    QJsonObject handleStationList(const QJsonObject &request)
    {
        const QString keyword = request.value(QStringLiteral("keyword")).toString().trimmed();
        const bool includeClosed = request.value(QStringLiteral("include_closed")).toBool(false);

        QString sql = QStringLiteral("SELECT id, name, address, latitude, longitude, status FROM stations");
        QStringList clauses;
        if (!includeClosed) {
            clauses.append(QStringLiteral("status = 'open'"));
        }
        if (!keyword.isEmpty()) {
            clauses.append(QStringLiteral("(name LIKE :keyword OR address LIKE :keyword)"));
        }
        if (!clauses.isEmpty()) {
            sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
        }
        sql += QStringLiteral(" ORDER BY id ASC");

        QSqlQuery query(db_);
        query.prepare(sql);
        if (!keyword.isEmpty()) {
            query.bindValue(QStringLiteral(":keyword"), QStringLiteral("%") + keyword + QStringLiteral("%"));
        }
        if (!query.exec()) {
            return errorResponse(QStringLiteral("station.list"), query.lastError().text());
        }

        QJsonArray stations;
        while (query.next()) {
            stations.append(stationToJson(query));
        }
        return okResponse(QStringLiteral("station.list"), {
            {QStringLiteral("stations"), stations},
        });
    }

    QJsonObject handleChargerList(const QJsonObject &request)
    {
        const int stationId = request.value(QStringLiteral("station_id")).toInt(-1);
        if (stationId <= 0) {
            return errorResponse(QStringLiteral("charger.list"), QStringLiteral("station_id is required"));
        }

        QSqlQuery stationQuery(db_);
        stationQuery.prepare(QStringLiteral("SELECT COUNT(*) AS count FROM stations WHERE id = :station_id"));
        stationQuery.bindValue(QStringLiteral(":station_id"), stationId);
        if (!stationQuery.exec() || !stationQuery.next()) {
            return errorResponse(QStringLiteral("charger.list"), stationQuery.lastError().text());
        }
        if (sqlValue(stationQuery, QStringLiteral("count")).toInt() == 0) {
            return errorResponse(QStringLiteral("charger.list"), QStringLiteral("station not found"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT c.id, c.station_id, s.name AS station_name, c.code, c.type, c.power_kw, c.status, "
            "       c.current_power_kw, c.total_orders, c.total_energy_kwh, c.total_duration_minutes "
            "FROM chargers c "
            "JOIN stations s ON s.id = c.station_id "
            "WHERE c.station_id = :station_id "
            "ORDER BY c.id ASC"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("charger.list"), query.lastError().text());
        }

        QJsonArray chargers;
        while (query.next()) {
            chargers.append(chargerToJson(query));
        }
        return okResponse(QStringLiteral("charger.list"), {
            {QStringLiteral("chargers"), chargers},
        });
    }

    QJsonObject handleOrderCreate(const QJsonObject &request)
    {
        int userId = 0;
        QString authError;
        if (!resolveUserId(request, QStringLiteral("order.create"), &userId, &authError)) {
            return errorResponse(QStringLiteral("order.create"), authError);
        }
        const int chargerId = requestInt(request, QStringLiteral("charger_id"));
        if (userId <= 0 || chargerId <= 0) {
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("user_id and charger_id are required"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("order.create"), db_.lastError().text());
        }

        QSqlQuery userQuery(db_);
        userQuery.prepare(QStringLiteral("SELECT status FROM users WHERE id = :user_id"));
        userQuery.bindValue(QStringLiteral(":user_id"), userId);
        if (!userQuery.exec() || !userQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("user not found"));
        }
        if (sqlValue(userQuery, QStringLiteral("status")).toString() != QStringLiteral("active")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("user account is frozen"));
        }

        QSqlQuery chargerQuery(db_);
        chargerQuery.prepare(QStringLiteral(
            "SELECT c.status AS charger_status, s.status AS station_status "
            "FROM chargers c JOIN stations s ON s.id = c.station_id "
            "WHERE c.id = :charger_id"));
        chargerQuery.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!chargerQuery.exec() || !chargerQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("charger not found"));
        }
        if (sqlValue(chargerQuery, QStringLiteral("station_status")).toString() != QStringLiteral("open")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("station is closed"));
        }
        if (sqlValue(chargerQuery, QStringLiteral("charger_status")).toString() != QStringLiteral("idle")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("charger is not available"));
        }

        QSqlQuery activeOrderQuery(db_);
        activeOrderQuery.prepare(QStringLiteral(
            "SELECT COUNT(*) AS count FROM charging_orders "
            "WHERE charger_id = :charger_id AND status IN ('reserved', 'charging')"));
        activeOrderQuery.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!activeOrderQuery.exec() || !activeOrderQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), activeOrderQuery.lastError().text());
        }
        if (sqlValue(activeOrderQuery, QStringLiteral("count")).toInt() > 0) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), QStringLiteral("charger already has an active order"));
        }

        const QString orderNo = makeOrderNo();
        QSqlQuery insert(db_);
        insert.prepare(QStringLiteral(
            "INSERT INTO charging_orders (order_no, user_id, charger_id, status, reserved_at, created_at, updated_at) "
            "VALUES (:order_no, :user_id, :charger_id, 'reserved', :now, :now, :now)"));
        insert.bindValue(QStringLiteral(":order_no"), orderNo);
        insert.bindValue(QStringLiteral(":user_id"), userId);
        insert.bindValue(QStringLiteral(":charger_id"), chargerId);
        insert.bindValue(QStringLiteral(":now"), nowUtc());
        if (!insert.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.create"), insert.lastError().text());
        }
        const int orderId = insert.lastInsertId().toInt();

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("order.create"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("order.create"), {
            {QStringLiteral("order"), fetchOrder(orderId)},
        });
    }

    QJsonObject handleOrderStart(const QJsonObject &request)
    {
        const int orderId = requestInt(request, QStringLiteral("order_id"));
        if (orderId <= 0) {
            return errorResponse(QStringLiteral("order.start"), QStringLiteral("order_id is required"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("order.start"), db_.lastError().text());
        }

        QSqlQuery orderQuery(db_);
        orderQuery.prepare(QStringLiteral("SELECT user_id, charger_id, status FROM charging_orders WHERE id = :order_id"));
        orderQuery.bindValue(QStringLiteral(":order_id"), orderId);
        if (!orderQuery.exec() || !orderQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), QStringLiteral("order not found"));
        }
        QString authError;
        if (!authorizeOrderAccess(request, sqlValue(orderQuery, QStringLiteral("user_id")).toInt(), true, &authError)) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), authError);
        }
        if (sqlValue(orderQuery, QStringLiteral("status")).toString() != QStringLiteral("reserved")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), QStringLiteral("order is not reserved"));
        }

        const int chargerId = sqlValue(orderQuery, QStringLiteral("charger_id")).toInt();
        QSqlQuery chargerQuery(db_);
        chargerQuery.prepare(QStringLiteral("SELECT status FROM chargers WHERE id = :charger_id"));
        chargerQuery.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!chargerQuery.exec() || !chargerQuery.next()
            || sqlValue(chargerQuery, QStringLiteral("status")).toString() != QStringLiteral("idle")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), QStringLiteral("charger is not available"));
        }

        const QString now = nowUtc();
        QSqlQuery updateOrder(db_);
        updateOrder.prepare(QStringLiteral(
            "UPDATE charging_orders SET status = 'charging', started_at = :now, updated_at = :now "
            "WHERE id = :order_id"));
        updateOrder.bindValue(QStringLiteral(":now"), now);
        updateOrder.bindValue(QStringLiteral(":order_id"), orderId);
        if (!updateOrder.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), updateOrder.lastError().text());
        }

        QSqlQuery updateCharger(db_);
        updateCharger.prepare(QStringLiteral(
            "UPDATE chargers SET status = 'charging', current_power_kw = power_kw, updated_at = :now "
            "WHERE id = :charger_id"));
        updateCharger.bindValue(QStringLiteral(":now"), now);
        updateCharger.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!updateCharger.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.start"), updateCharger.lastError().text());
        }

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("order.start"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("order.start"), {
            {QStringLiteral("order"), fetchOrder(orderId)},
        });
    }

    QJsonObject handleOrderStop(const QJsonObject &request)
    {
        const int orderId = requestInt(request, QStringLiteral("order_id"));
        double energyKwh = requestDouble(request, QStringLiteral("energy_kwh"), 1.0);
        if (orderId <= 0) {
            return errorResponse(QStringLiteral("order.stop"), QStringLiteral("order_id is required"));
        }
        if (energyKwh < 0) {
            return errorResponse(QStringLiteral("order.stop"), QStringLiteral("energy_kwh cannot be negative"));
        }
        if (energyKwh == 0) {
            energyKwh = 1.0;
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("order.stop"), db_.lastError().text());
        }

        QSqlQuery orderQuery(db_);
        orderQuery.prepare(QStringLiteral("SELECT user_id, charger_id, status FROM charging_orders WHERE id = :order_id"));
        orderQuery.bindValue(QStringLiteral(":order_id"), orderId);
        if (!orderQuery.exec() || !orderQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), QStringLiteral("order not found"));
        }
        QString authError;
        if (!authorizeOrderAccess(request, sqlValue(orderQuery, QStringLiteral("user_id")).toInt(), true, &authError)) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), authError);
        }
        if (sqlValue(orderQuery, QStringLiteral("status")).toString() != QStringLiteral("charging")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), QStringLiteral("order is not charging"));
        }

        const int chargerId = sqlValue(orderQuery, QStringLiteral("charger_id")).toInt();
        const QString now = nowUtc();
        QSqlQuery updateOrder(db_);
        updateOrder.prepare(QStringLiteral(
            "UPDATE charging_orders "
            "SET status = 'pending_settlement', stopped_at = :now, energy_kwh = :energy_kwh, updated_at = :now "
            "WHERE id = :order_id"));
        updateOrder.bindValue(QStringLiteral(":now"), now);
        updateOrder.bindValue(QStringLiteral(":energy_kwh"), energyKwh);
        updateOrder.bindValue(QStringLiteral(":order_id"), orderId);
        if (!updateOrder.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), updateOrder.lastError().text());
        }

        QSqlQuery updateCharger(db_);
        updateCharger.prepare(QStringLiteral(
            "UPDATE chargers SET status = 'idle', current_power_kw = 0, updated_at = :now WHERE id = :charger_id"));
        updateCharger.bindValue(QStringLiteral(":now"), now);
        updateCharger.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!updateCharger.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), updateCharger.lastError().text());
        }

        QSqlQuery telemetry(db_);
        telemetry.prepare(QStringLiteral(
            "INSERT INTO charger_telemetry (charger_id, status, power_kw, energy_kwh, recorded_at) "
            "VALUES (:charger_id, 'idle', 0, :energy_kwh, :now)"));
        telemetry.bindValue(QStringLiteral(":charger_id"), chargerId);
        telemetry.bindValue(QStringLiteral(":energy_kwh"), energyKwh);
        telemetry.bindValue(QStringLiteral(":now"), now);
        if (!telemetry.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.stop"), telemetry.lastError().text());
        }

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("order.stop"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("order.stop"), {
            {QStringLiteral("order"), fetchOrder(orderId)},
        });
    }

    QJsonObject handleOrderCancel(const QJsonObject &request)
    {
        const int orderId = requestInt(request, QStringLiteral("order_id"));
        if (orderId <= 0) {
            return errorResponse(QStringLiteral("order.cancel"), QStringLiteral("order_id is required"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("order.cancel"), db_.lastError().text());
        }

        QSqlQuery orderQuery(db_);
        orderQuery.prepare(QStringLiteral("SELECT user_id, charger_id, status FROM charging_orders WHERE id = :order_id"));
        orderQuery.bindValue(QStringLiteral(":order_id"), orderId);
        if (!orderQuery.exec() || !orderQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.cancel"), QStringLiteral("order not found"));
        }

        QString authError;
        if (!authorizeOrderAccess(request, sqlValue(orderQuery, QStringLiteral("user_id")).toInt(), true, &authError)) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.cancel"), authError);
        }
        if (sqlValue(orderQuery, QStringLiteral("status")).toString() != QStringLiteral("reserved")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.cancel"), QStringLiteral("only reserved orders can be cancelled"));
        }

        const int chargerId = sqlValue(orderQuery, QStringLiteral("charger_id")).toInt();
        const QString now = nowUtc();
        QSqlQuery updateOrder(db_);
        updateOrder.prepare(QStringLiteral(
            "UPDATE charging_orders SET status = 'cancelled', updated_at = :now WHERE id = :order_id"));
        updateOrder.bindValue(QStringLiteral(":now"), now);
        updateOrder.bindValue(QStringLiteral(":order_id"), orderId);
        if (!updateOrder.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.cancel"), updateOrder.lastError().text());
        }

        QSqlQuery updateCharger(db_);
        updateCharger.prepare(QStringLiteral(
            "UPDATE chargers SET status = 'idle', current_power_kw = 0, updated_at = :now "
            "WHERE id = :charger_id AND status <> 'charging'"));
        updateCharger.bindValue(QStringLiteral(":now"), now);
        updateCharger.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!updateCharger.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.cancel"), updateCharger.lastError().text());
        }

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("order.cancel"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("order.cancel"), {
            {QStringLiteral("order"), fetchOrder(orderId)},
        });
    }

    QJsonObject handleOrderSettle(const QJsonObject &request)
    {
        const int orderId = requestInt(request, QStringLiteral("order_id"));
        if (orderId <= 0) {
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("order_id is required"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("order.settle"), db_.lastError().text());
        }

        QSqlQuery orderQuery(db_);
        orderQuery.prepare(QStringLiteral(
            "SELECT id, user_id, charger_id, status, started_at, stopped_at, energy_kwh "
            "FROM charging_orders WHERE id = :order_id"));
        orderQuery.bindValue(QStringLiteral(":order_id"), orderId);
        if (!orderQuery.exec() || !orderQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("order not found"));
        }
        QString authError;
        if (!authorizeOrderAccess(request, sqlValue(orderQuery, QStringLiteral("user_id")).toInt(), true, &authError)) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), authError);
        }
        if (sqlValue(orderQuery, QStringLiteral("status")).toString() != QStringLiteral("pending_settlement")) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("order is not pending settlement"));
        }

        const int userId = sqlValue(orderQuery, QStringLiteral("user_id")).toInt();
        const int chargerId = sqlValue(orderQuery, QStringLiteral("charger_id")).toInt();
        double energyKwh = requestDouble(request, QStringLiteral("energy_kwh"),
                                        sqlValue(orderQuery, QStringLiteral("energy_kwh")).toDouble());
        if (energyKwh <= 0) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("energy_kwh must be greater than zero"));
        }

        const int amountCents = static_cast<int>(std::llround(energyKwh * kEnergyPriceCentsPerKwh));
        QSqlQuery userQuery(db_);
        userQuery.prepare(QStringLiteral("SELECT balance_cents FROM users WHERE id = :user_id"));
        userQuery.bindValue(QStringLiteral(":user_id"), userId);
        if (!userQuery.exec() || !userQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("user not found"));
        }

        const int balance = sqlValue(userQuery, QStringLiteral("balance_cents")).toInt();
        if (balance < amountCents) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), QStringLiteral("insufficient balance"));
        }
        const int balanceAfter = balance - amountCents;
        const QString now = nowUtc();

        QSqlQuery updateUser(db_);
        updateUser.prepare(QStringLiteral(
            "UPDATE users SET balance_cents = :balance_after, updated_at = :now WHERE id = :user_id"));
        updateUser.bindValue(QStringLiteral(":balance_after"), balanceAfter);
        updateUser.bindValue(QStringLiteral(":now"), now);
        updateUser.bindValue(QStringLiteral(":user_id"), userId);
        if (!updateUser.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), updateUser.lastError().text());
        }

        QSqlQuery updateOrder(db_);
        updateOrder.prepare(QStringLiteral(
            "UPDATE charging_orders "
            "SET status = 'completed', settled_at = :now, energy_kwh = :energy_kwh, amount_cents = :amount_cents, "
            "    updated_at = :now "
            "WHERE id = :order_id"));
        updateOrder.bindValue(QStringLiteral(":now"), now);
        updateOrder.bindValue(QStringLiteral(":energy_kwh"), energyKwh);
        updateOrder.bindValue(QStringLiteral(":amount_cents"), amountCents);
        updateOrder.bindValue(QStringLiteral(":order_id"), orderId);
        if (!updateOrder.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), updateOrder.lastError().text());
        }

        const int minutes = durationMinutes(sqlValue(orderQuery, QStringLiteral("started_at")).toString(),
                                            sqlValue(orderQuery, QStringLiteral("stopped_at")).toString());
        QSqlQuery updateCharger(db_);
        updateCharger.prepare(QStringLiteral(
            "UPDATE chargers "
            "SET total_orders = total_orders + 1, "
            "    total_energy_kwh = total_energy_kwh + :energy_kwh, "
            "    total_duration_minutes = total_duration_minutes + :minutes, "
            "    updated_at = :now "
            "WHERE id = :charger_id"));
        updateCharger.bindValue(QStringLiteral(":energy_kwh"), energyKwh);
        updateCharger.bindValue(QStringLiteral(":minutes"), minutes);
        updateCharger.bindValue(QStringLiteral(":now"), now);
        updateCharger.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!updateCharger.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), updateCharger.lastError().text());
        }

        QSqlQuery log(db_);
        log.prepare(QStringLiteral(
            "INSERT INTO balance_logs (user_id, change_cents, balance_after_cents, reason, related_order_id, created_at) "
            "VALUES (:user_id, :change_cents, :balance_after, 'charge_settlement', :order_id, :now)"));
        log.bindValue(QStringLiteral(":user_id"), userId);
        log.bindValue(QStringLiteral(":change_cents"), -amountCents);
        log.bindValue(QStringLiteral(":balance_after"), balanceAfter);
        log.bindValue(QStringLiteral(":order_id"), orderId);
        log.bindValue(QStringLiteral(":now"), now);
        if (!log.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("order.settle"), log.lastError().text());
        }

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("order.settle"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("order.settle"), {
            {QStringLiteral("order"), fetchOrder(orderId)},
            {QStringLiteral("balance_cents"), balanceAfter},
            {QStringLiteral("balance_yuan"), balanceAfter / 100.0},
            {QStringLiteral("price_cents_per_kwh"), kEnergyPriceCentsPerKwh},
        });
    }

    QJsonObject handleOrderCurrent(const QJsonObject &request)
    {
        int userId = 0;
        QString authError;
        if (!resolveUserId(request, QStringLiteral("order.current"), &userId, &authError)) {
            return errorResponse(QStringLiteral("order.current"), authError);
        }

        QSqlQuery query(db_);
        query.prepare(orderSelectSql()
                      + QStringLiteral(
                          " WHERE o.user_id = :user_id AND o.status IN ('reserved', 'charging', 'pending_settlement') "
                          "ORDER BY o.id DESC LIMIT 1"));
        query.bindValue(QStringLiteral(":user_id"), userId);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("order.current"), query.lastError().text());
        }
        if (!query.next()) {
            return okResponse(QStringLiteral("order.current"), {
                {QStringLiteral("order"), QJsonValue::Null},
            });
        }

        const QJsonObject order = orderToJson(query);
        const int chargerId = sqlValue(query, QStringLiteral("charger_id")).toInt();
        const double energyKwh = latestTelemetryEnergy(chargerId);
        const int estimatedAmountCents = static_cast<int>(std::llround(energyKwh * kEnergyPriceCentsPerKwh));

        return okResponse(QStringLiteral("order.current"), {
            {QStringLiteral("order"), order},
            {QStringLiteral("latest_telemetry"), fetchLatestTelemetry(chargerId)},
            {QStringLiteral("estimated_energy_kwh"), energyKwh},
            {QStringLiteral("estimated_amount_cents"), estimatedAmountCents},
            {QStringLiteral("estimated_amount_yuan"), estimatedAmountCents / 100.0},
            {QStringLiteral("price_cents_per_kwh"), kEnergyPriceCentsPerKwh},
        });
    }

    QJsonObject handleOrderList(const QJsonObject &request)
    {
        QString actorType;
        int actorId = 0;
        QString authError;
        if (!readOptionalSession(request, &actorType, &actorId, &authError)) {
            return errorResponse(QStringLiteral("order.list"), authError);
        }

        int userId = requestInt(request, QStringLiteral("user_id"), 0);
        if (actorType == QStringLiteral("user")) {
            if (userId > 0 && userId != actorId) {
                return errorResponse(QStringLiteral("order.list"), QStringLiteral("users can only list their own orders"));
            }
            userId = actorId;
        }
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        if (!status.isEmpty() && !isValidOrderStatus(status)) {
            return errorResponse(QStringLiteral("order.list"), QStringLiteral("invalid order status"));
        }
        const int limit = std::clamp(requestInt(request, QStringLiteral("limit"), 50), 1, 200);

        QString sql = orderSelectSql();
        QStringList clauses;
        if (userId > 0) {
            clauses.append(QStringLiteral("o.user_id = :user_id"));
        }
        if (!status.isEmpty()) {
            clauses.append(QStringLiteral("o.status = :status"));
        }
        if (!clauses.isEmpty()) {
            sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
        }
        sql += QStringLiteral(" ORDER BY o.id DESC LIMIT :limit");

        QSqlQuery query(db_);
        query.prepare(sql);
        if (userId > 0) {
            query.bindValue(QStringLiteral(":user_id"), userId);
        }
        if (!status.isEmpty()) {
            query.bindValue(QStringLiteral(":status"), status);
        }
        query.bindValue(QStringLiteral(":limit"), limit);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("order.list"), query.lastError().text());
        }

        QJsonArray orders;
        while (query.next()) {
            orders.append(orderToJson(query));
        }

        return okResponse(QStringLiteral("order.list"), {
            {QStringLiteral("orders"), orders},
        });
    }

    QJsonObject handleBalanceRecharge(const QJsonObject &request)
    {
        int userId = 0;
        QString authError;
        if (!resolveUserId(request, QStringLiteral("balance.recharge"), &userId, &authError)) {
            return errorResponse(QStringLiteral("balance.recharge"), authError);
        }
        int amountCents = requestInt(request, QStringLiteral("amount_cents"), 0);
        if (amountCents <= 0) {
            const double amountYuan = requestDouble(request, QStringLiteral("amount"), 0.0);
            amountCents = static_cast<int>(std::llround(amountYuan * 100.0));
        }

        if (userId <= 0 || amountCents <= 0) {
            return errorResponse(QStringLiteral("balance.recharge"), QStringLiteral("user_id and positive amount are required"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("balance.recharge"), db_.lastError().text());
        }

        QSqlQuery userQuery(db_);
        userQuery.prepare(QStringLiteral("SELECT balance_cents FROM users WHERE id = :user_id"));
        userQuery.bindValue(QStringLiteral(":user_id"), userId);
        if (!userQuery.exec() || !userQuery.next()) {
            db_.rollback();
            return errorResponse(QStringLiteral("balance.recharge"), QStringLiteral("user not found"));
        }

        const int balanceAfter = sqlValue(userQuery, QStringLiteral("balance_cents")).toInt() + amountCents;
        const QString now = nowUtc();
        QSqlQuery updateUser(db_);
        updateUser.prepare(QStringLiteral(
            "UPDATE users SET balance_cents = :balance_after, updated_at = :now WHERE id = :user_id"));
        updateUser.bindValue(QStringLiteral(":balance_after"), balanceAfter);
        updateUser.bindValue(QStringLiteral(":now"), now);
        updateUser.bindValue(QStringLiteral(":user_id"), userId);
        if (!updateUser.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("balance.recharge"), updateUser.lastError().text());
        }

        QSqlQuery log(db_);
        log.prepare(QStringLiteral(
            "INSERT INTO balance_logs (user_id, change_cents, balance_after_cents, reason, created_at) "
            "VALUES (:user_id, :change_cents, :balance_after, 'recharge', :now)"));
        log.bindValue(QStringLiteral(":user_id"), userId);
        log.bindValue(QStringLiteral(":change_cents"), amountCents);
        log.bindValue(QStringLiteral(":balance_after"), balanceAfter);
        log.bindValue(QStringLiteral(":now"), now);
        if (!log.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("balance.recharge"), log.lastError().text());
        }

        if (!db_.commit()) {
            return errorResponse(QStringLiteral("balance.recharge"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("balance.recharge"), {
            {QStringLiteral("user_id"), userId},
            {QStringLiteral("balance_cents"), balanceAfter},
            {QStringLiteral("balance_yuan"), balanceAfter / 100.0},
            {QStringLiteral("log_id"), log.lastInsertId().toInt()},
        });
    }

    QJsonObject handleBalanceLogs(const QJsonObject &request)
    {
        QString actorType;
        int actorId = 0;
        QString authError;
        if (!readOptionalSession(request, &actorType, &actorId, &authError)) {
            return errorResponse(QStringLiteral("balance.logs"), authError);
        }

        int userId = requestInt(request, QStringLiteral("user_id"), 0);
        if (actorType == QStringLiteral("user")) {
            if (userId > 0 && userId != actorId) {
                return errorResponse(QStringLiteral("balance.logs"), QStringLiteral("users can only list their own balance logs"));
            }
            userId = actorId;
        }
        const int limit = std::clamp(requestInt(request, QStringLiteral("limit"), 50), 1, 200);

        QString sql = QStringLiteral(
            "SELECT b.id, b.user_id, u.phone AS user_phone, b.change_cents, b.balance_after_cents, "
            "       b.reason, b.related_order_id, b.created_at "
            "FROM balance_logs b JOIN users u ON u.id = b.user_id");
        if (userId > 0) {
            sql += QStringLiteral(" WHERE b.user_id = :user_id");
        }
        sql += QStringLiteral(" ORDER BY b.id DESC LIMIT :limit");

        QSqlQuery query(db_);
        query.prepare(sql);
        if (userId > 0) {
            query.bindValue(QStringLiteral(":user_id"), userId);
        }
        query.bindValue(QStringLiteral(":limit"), limit);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("balance.logs"), query.lastError().text());
        }

        QJsonArray logs;
        while (query.next()) {
            logs.append(balanceLogToJson(query));
        }

        return okResponse(QStringLiteral("balance.logs"), {
            {QStringLiteral("logs"), logs},
        });
    }

    QJsonObject handleTelemetryReport(const QJsonObject &request)
    {
        const int chargerId = requestInt(request, QStringLiteral("charger_id"));
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        const double powerKw = requestDouble(request, QStringLiteral("power_kw"), -1.0);
        const double energyKwh = requestDouble(request, QStringLiteral("energy_kwh"), -1.0);

        if (chargerId <= 0 || !isValidChargerStatus(status) || powerKw < 0 || energyKwh < 0) {
            return errorResponse(QStringLiteral("telemetry.report"),
                                 QStringLiteral("charger_id, status, non-negative power_kw and energy_kwh are required"));
        }
        if (fetchCharger(chargerId).isEmpty()) {
            return errorResponse(QStringLiteral("telemetry.report"), QStringLiteral("charger not found"));
        }

        if (!db_.transaction()) {
            return errorResponse(QStringLiteral("telemetry.report"), db_.lastError().text());
        }

        const QString now = nowUtc();
        QSqlQuery telemetry(db_);
        telemetry.prepare(QStringLiteral(
            "INSERT INTO charger_telemetry (charger_id, status, power_kw, energy_kwh, recorded_at) "
            "VALUES (:charger_id, :status, :power_kw, :energy_kwh, :now)"));
        telemetry.bindValue(QStringLiteral(":charger_id"), chargerId);
        telemetry.bindValue(QStringLiteral(":status"), status);
        telemetry.bindValue(QStringLiteral(":power_kw"), powerKw);
        telemetry.bindValue(QStringLiteral(":energy_kwh"), energyKwh);
        telemetry.bindValue(QStringLiteral(":now"), now);
        if (!telemetry.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("telemetry.report"), telemetry.lastError().text());
        }

        QSqlQuery update(db_);
        update.prepare(QStringLiteral(
            "UPDATE chargers SET status = :status, current_power_kw = :power_kw, updated_at = :now "
            "WHERE id = :charger_id"));
        update.bindValue(QStringLiteral(":status"), status);
        update.bindValue(QStringLiteral(":power_kw"), powerKw);
        update.bindValue(QStringLiteral(":now"), now);
        update.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!update.exec()) {
            db_.rollback();
            return errorResponse(QStringLiteral("telemetry.report"), update.lastError().text());
        }

        const int telemetryId = telemetry.lastInsertId().toInt();
        if (!db_.commit()) {
            return errorResponse(QStringLiteral("telemetry.report"), db_.lastError().text());
        }

        return okResponse(QStringLiteral("telemetry.report"), {
            {QStringLiteral("telemetry_id"), telemetryId},
            {QStringLiteral("charger"), fetchCharger(chargerId)},
        });
    }

    QJsonObject handleTelemetryList(const QJsonObject &request)
    {
        const int chargerId = requestInt(request, QStringLiteral("charger_id"));
        const int limit = std::clamp(requestInt(request, QStringLiteral("limit"), 50), 1, 200);
        if (chargerId <= 0) {
            return errorResponse(QStringLiteral("telemetry.list"), QStringLiteral("charger_id is required"));
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT t.id, t.charger_id, c.code AS charger_code, t.status, t.power_kw, t.energy_kwh, t.recorded_at "
            "FROM charger_telemetry t JOIN chargers c ON c.id = t.charger_id "
            "WHERE t.charger_id = :charger_id "
            "ORDER BY t.id DESC LIMIT :limit"));
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        query.bindValue(QStringLiteral(":limit"), limit);
        if (!query.exec()) {
            return errorResponse(QStringLiteral("telemetry.list"), query.lastError().text());
        }

        QJsonArray records;
        while (query.next()) {
            records.append(telemetryToJson(query));
        }

        return okResponse(QStringLiteral("telemetry.list"), {
            {QStringLiteral("telemetry"), records},
        });
    }

    QString orderSelectSql() const
    {
        return QStringLiteral(
            "SELECT o.id, o.order_no, o.user_id, u.phone AS user_phone, "
            "       o.charger_id, c.code AS charger_code, s.name AS station_name, "
            "       o.status, o.reserved_at, o.started_at, o.stopped_at, o.settled_at, "
            "       o.energy_kwh, o.amount_cents "
            "FROM charging_orders o "
            "JOIN users u ON u.id = o.user_id "
            "JOIN chargers c ON c.id = o.charger_id "
            "JOIN stations s ON s.id = c.station_id");
    }

    QJsonObject fetchOrder(int orderId)
    {
        QSqlQuery query(db_);
        query.prepare(orderSelectSql() + QStringLiteral(" WHERE o.id = :order_id"));
        query.bindValue(QStringLiteral(":order_id"), orderId);
        if (!query.exec() || !query.next()) {
            return {};
        }
        return orderToJson(query);
    }

    QString chargerSelectSql() const
    {
        return QStringLiteral(
            "SELECT c.id, c.station_id, s.name AS station_name, c.code, c.type, c.power_kw, c.status, "
            "       c.current_power_kw, c.total_orders, c.total_energy_kwh, c.total_duration_minutes "
            "FROM chargers c "
            "JOIN stations s ON s.id = c.station_id");
    }

    QJsonObject fetchStation(int stationId)
    {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT id, name, address, latitude, longitude, status FROM stations WHERE id = :station_id"));
        query.bindValue(QStringLiteral(":station_id"), stationId);
        if (!query.exec() || !query.next()) {
            return {};
        }
        return stationToJson(query);
    }

    QJsonObject fetchCharger(int chargerId)
    {
        QSqlQuery query(db_);
        query.prepare(chargerSelectSql() + QStringLiteral(" WHERE c.id = :charger_id"));
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!query.exec() || !query.next()) {
            return {};
        }
        return chargerToJson(query);
    }

    QJsonObject fetchLatestTelemetry(int chargerId)
    {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT t.id, t.charger_id, c.code AS charger_code, t.status, t.power_kw, t.energy_kwh, t.recorded_at "
            "FROM charger_telemetry t JOIN chargers c ON c.id = t.charger_id "
            "WHERE t.charger_id = :charger_id "
            "ORDER BY t.id DESC LIMIT 1"));
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!query.exec() || !query.next()) {
            return {};
        }
        return telemetryToJson(query);
    }

    double latestTelemetryEnergy(int chargerId)
    {
        const QJsonObject latest = fetchLatestTelemetry(chargerId);
        if (latest.isEmpty()) {
            return 0.0;
        }
        return latest.value(QStringLiteral("energy_kwh")).toDouble(0.0);
    }

    bool chargerCodeExists(const QString &code, int exceptChargerId = 0)
    {
        QString sql = QStringLiteral("SELECT COUNT(*) AS count FROM chargers WHERE code = :code");
        if (exceptChargerId > 0) {
            sql += QStringLiteral(" AND id <> :charger_id");
        }

        QSqlQuery query(db_);
        query.prepare(sql);
        query.bindValue(QStringLiteral(":code"), code);
        if (exceptChargerId > 0) {
            query.bindValue(QStringLiteral(":charger_id"), exceptChargerId);
        }
        if (!query.exec() || !query.next()) {
            return true;
        }
        return sqlValue(query, QStringLiteral("count")).toInt() > 0;
    }

    bool chargerHasActiveOrder(int chargerId)
    {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) AS count FROM charging_orders "
            "WHERE charger_id = :charger_id AND status IN ('reserved', 'charging')"));
        query.bindValue(QStringLiteral(":charger_id"), chargerId);
        if (!query.exec() || !query.next()) {
            return true;
        }
        return sqlValue(query, QStringLiteral("count")).toInt() > 0;
    }

    void bindStationValues(QSqlQuery &query, const QString &name, const QString &address,
                           double latitude, double longitude, const QString &status)
    {
        query.bindValue(QStringLiteral(":name"), name);
        query.bindValue(QStringLiteral(":address"), address);
        query.bindValue(QStringLiteral(":latitude"), latitude);
        query.bindValue(QStringLiteral(":longitude"), longitude);
        query.bindValue(QStringLiteral(":status"), status);
    }

    void bindChargerValues(QSqlQuery &query, int stationId, const QString &code, const QString &type,
                           double powerKw, const QString &status)
    {
        query.bindValue(QStringLiteral(":station_id"), stationId);
        query.bindValue(QStringLiteral(":code"), code);
        query.bindValue(QStringLiteral(":type"), type);
        query.bindValue(QStringLiteral(":power_kw"), powerKw);
        query.bindValue(QStringLiteral(":status"), status);
    }

    QJsonObject createSession(const QString &actorType, int actorId, QString *error)
    {
        const QString token = makeSessionToken(actorType, actorId);
        const QString tokenHash = sha256Hex(token);
        const QString createdAt = nowUtc();
        const QString expiresAt = QDateTime::currentDateTimeUtc().addSecs(8 * 60 * 60).toString(Qt::ISODateWithMs);

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "INSERT INTO auth_sessions (actor_type, actor_id, token_hash, created_at, expires_at) "
            "VALUES (:actor_type, :actor_id, :token_hash, :created_at, :expires_at)"));
        query.bindValue(QStringLiteral(":actor_type"), actorType);
        query.bindValue(QStringLiteral(":actor_id"), actorId);
        query.bindValue(QStringLiteral(":token_hash"), tokenHash);
        query.bindValue(QStringLiteral(":created_at"), createdAt);
        query.bindValue(QStringLiteral(":expires_at"), expiresAt);
        if (!query.exec()) {
            *error = query.lastError().text();
            return {};
        }

        return {
            {QStringLiteral("token"), token},
            {QStringLiteral("expires_at"), expiresAt},
        };
    }

    bool requireSession(const QJsonObject &request, const QString &actorType, int *actorId, QString *error)
    {
        const QString token = request.value(QStringLiteral("session_token")).toString().trimmed();
        if (token.isEmpty()) {
            *error = QStringLiteral("session_token is required");
            return false;
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT actor_id FROM auth_sessions "
            "WHERE actor_type = :actor_type AND token_hash = :token_hash AND expires_at > :now "
            "ORDER BY id DESC LIMIT 1"));
        query.bindValue(QStringLiteral(":actor_type"), actorType);
        query.bindValue(QStringLiteral(":token_hash"), sha256Hex(token));
        query.bindValue(QStringLiteral(":now"), nowUtc());
        if (!query.exec()) {
            *error = query.lastError().text();
            return false;
        }
        if (!query.next()) {
            *error = QStringLiteral("invalid or expired session");
            return false;
        }

        *actorId = sqlValue(query, QStringLiteral("actor_id")).toInt();
        return true;
    }

    bool readOptionalSession(const QJsonObject &request, QString *actorType, int *actorId, QString *error)
    {
        const QString token = request.value(QStringLiteral("session_token")).toString().trimmed();
        actorType->clear();
        *actorId = 0;

        if (token.isEmpty()) {
            return true;
        }

        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "SELECT actor_type, actor_id FROM auth_sessions "
            "WHERE token_hash = :token_hash AND expires_at > :now "
            "ORDER BY id DESC LIMIT 1"));
        query.bindValue(QStringLiteral(":token_hash"), sha256Hex(token));
        query.bindValue(QStringLiteral(":now"), nowUtc());
        if (!query.exec()) {
            *error = query.lastError().text();
            return false;
        }
        if (!query.next()) {
            *error = QStringLiteral("invalid or expired session");
            return false;
        }

        *actorType = sqlValue(query, QStringLiteral("actor_type")).toString();
        *actorId = sqlValue(query, QStringLiteral("actor_id")).toInt();
        return true;
    }

    bool resolveUserId(const QJsonObject &request, const QString &action, int *userId, QString *error)
    {
        QString actorType;
        int actorId = 0;
        if (!readOptionalSession(request, &actorType, &actorId, error)) {
            return false;
        }

        const int requestedUserId = requestInt(request, QStringLiteral("user_id"), 0);
        if (actorType == QStringLiteral("user")) {
            if (requestedUserId > 0 && requestedUserId != actorId) {
                *error = QStringLiteral("users can only operate on their own account");
                return false;
            }
            *userId = actorId;
            Q_UNUSED(action)
            return true;
        }

        *userId = requestedUserId;
        if (*userId <= 0) {
            *error = QStringLiteral("%1 requires session_token or user_id").arg(action);
            return false;
        }
        return true;
    }

    bool authorizeOrderAccess(const QJsonObject &request, int orderUserId, bool allowAdmin,
                              QString *error)
    {
        QString actorType;
        int actorId = 0;
        if (!readOptionalSession(request, &actorType, &actorId, error)) {
            return false;
        }

        if (actorType == QStringLiteral("admin")) {
            if (!allowAdmin) {
                *error = QStringLiteral("admin access is not allowed for this operation");
                return false;
            }
            return true;
        }

        if (actorType == QStringLiteral("user")) {
            if (actorId != orderUserId) {
                *error = QStringLiteral("users can only operate on their own orders");
                return false;
            }
            return true;
        }

        const int requestedUserId = requestInt(request, QStringLiteral("user_id"), 0);
        if (requestedUserId > 0 && requestedUserId == orderUserId) {
            return true;
        }

        *error = QStringLiteral("session_token or matching user_id is required");
        return false;
    }

    QSqlDatabase db_;
};

bool initializeDatabase(const QString &dbPath, const QString &schemaPath, const QString &seedPath, QString *error)
{
    const QFileInfo dbInfo(dbPath);
    if (!dbInfo.absoluteDir().exists() && !QDir().mkpath(dbInfo.absolutePath())) {
        *error = QStringLiteral("failed to create database directory: %1").arg(dbInfo.absolutePath());
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        *error = db.lastError().text();
        return false;
    }

    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        *error = pragma.lastError().text();
        return false;
    }

    if (!executeScript(db, schemaPath, error)) {
        return false;
    }
    if (!seedPath.isEmpty() && !executeScript(db, seedPath, error)) {
        return false;
    }
    if (!cleanupExpiredSessions(db, error)) {
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("charging-server"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("TCP JSON service for the intelligent charging platform"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("port")},
                                        QStringLiteral("TCP listen port"),
                                        QStringLiteral("port"),
                                        QString::number(kDefaultPort)));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("db")},
                                        QStringLiteral("SQLite database path"),
                                        QStringLiteral("path"),
                                        QStringLiteral("data/charging.sqlite")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("schema")},
                                        QStringLiteral("Schema SQL path"),
                                        QStringLiteral("path"),
                                        QStringLiteral("resources/schema.sql")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("seed")},
                                        QStringLiteral("Seed SQL path"),
                                        QStringLiteral("path"),
                                        QStringLiteral("resources/seed.sql")));
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("init-only")},
                                        QStringLiteral("Initialize the database and exit without listening.")));
    parser.process(app);

    bool ok = false;
    const quint16 port = parser.value(QStringLiteral("port")).toUShort(&ok);
    if (!ok || port == 0) {
        qCritical().noquote() << "invalid port:" << parser.value(QStringLiteral("port"));
        return 2;
    }

    const QString dbPath = parser.value(QStringLiteral("db"));
    const QString schemaPath = parser.value(QStringLiteral("schema"));
    const QString seedPath = parser.value(QStringLiteral("seed"));

    QString error;
    if (!initializeDatabase(dbPath, schemaPath, seedPath, &error)) {
        qCritical().noquote() << "database initialization failed:" << error;
        return 3;
    }

    if (parser.isSet(QStringLiteral("init-only"))) {
        qInfo().noquote() << "database initialized:" << QFileInfo(dbPath).absoluteFilePath();
        return 0;
    }

    ChargingServer server(QSqlDatabase::database(kConnectionName));
    if (!server.listen(QHostAddress::Any, port)) {
        qCritical().noquote() << "listen failed on port" << port << server.errorString();
        return 4;
    }

    qInfo().noquote() << "database:" << QFileInfo(dbPath).absoluteFilePath();
    qInfo().noquote() << "listening:" << port;
    return app.exec();
}

#include "main.moc"
