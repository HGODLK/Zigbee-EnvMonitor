#ifndef DATABRIDGESERVER_H
#define DATABRIDGESERVER_H

#include <QObject>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>
#include "dataparser.h"

class DataBridgeServer : public QObject
{
    Q_OBJECT
public:
    explicit DataBridgeServer(QObject *parent = nullptr);

    bool start(quint16 port = 25577);
    void publishStatus(bool connected, const QString &host,
                       const QString &status, const QString &error = QString());
    void publishSensor(const SensorData &data);
    QString errorString() const;

private slots:
    void onNewConnection();

private:
    void broadcast(const QJsonObject &object);
    void sendObject(QTcpSocket *socket, const QJsonObject &object);

    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
    QJsonObject m_lastStatus;
    QJsonObject m_lastSensor;
};

#endif
