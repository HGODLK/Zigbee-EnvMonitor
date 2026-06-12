#include "databridgeserver.h"
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>

DataBridgeServer::DataBridgeServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    m_lastStatus = {
        {"type", "status"},
        {"connected", false},
        {"host", ""},
        {"status", "设备未连接"},
        {"error", ""}
    };
    connect(m_server, &QTcpServer::newConnection,
            this, &DataBridgeServer::onNewConnection);
}

bool DataBridgeServer::start(quint16 port)
{
    return m_server->listen(QHostAddress::Any, port);
}

void DataBridgeServer::publishStatus(bool connected, const QString &host,
                                     const QString &status, const QString &error)
{
    m_lastStatus = {
        {"type", "status"},
        {"connected", connected},
        {"host", host},
        {"status", status},
        {"error", error}
    };
    broadcast(m_lastStatus);
}

void DataBridgeServer::publishSensor(const SensorData &data)
{
    if (!data.valid)
        return;

    m_lastSensor = {
        {"type", "sensor"},
        {"temp", data.temp},
        {"humi", data.humi},
        {"light", data.light},
        {"timestamp", data.time.toMSecsSinceEpoch()}
    };
    broadcast(m_lastSensor);
}

QString DataBridgeServer::errorString() const
{
    return m_server->errorString();
}

void DataBridgeServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_clients.append(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_clients.removeAll(socket);
            socket->deleteLater();
        });
        sendObject(socket, m_lastStatus);
        if (!m_lastSensor.isEmpty())
            sendObject(socket, m_lastSensor);
    }
}

void DataBridgeServer::broadcast(const QJsonObject &object)
{
    const auto clients = m_clients;
    for (QTcpSocket *socket : clients) {
        if (socket->state() == QAbstractSocket::ConnectedState)
            sendObject(socket, object);
    }
}

void DataBridgeServer::sendObject(QTcpSocket *socket, const QJsonObject &object)
{
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket->write(payload);
}
