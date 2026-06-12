#include "tcpclient.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>
#include <QNetworkInterface>
#include <QtConcurrent/QtConcurrent>

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_port(25576)
{
    m_reconnectTimer->setInterval(3000);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected,
            this, &TcpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &TcpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &TcpClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &TcpClient::onError);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_intentional && !m_host.isEmpty())
            connectToHost(m_host, m_port);
    });
}

TcpClient::~TcpClient()
{
    m_intentional = true;
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
}

void TcpClient::connectToHost(const QString &host, quint16 port)
{
    m_intentional = false;
    m_reconnectTimer->stop();
    m_host = host;
    m_port = port;
    m_buf.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(host, port);
}

void TcpClient::disconnect()
{
    m_intentional = true;
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
}

bool TcpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClient::isIntentionalDisconnect() const { return m_intentional; }

QString TcpClient::host() const { return m_host; }

void TcpClient::onConnected()
{
    m_reconnectTimer->stop();
    emit connected(m_host);
}

void TcpClient::onDisconnected()
{
    if (!m_intentional && !m_host.isEmpty())
        m_reconnectTimer->start();
    emit disconnected();
}

void TcpClient::onReadyRead()
{
    m_buf += m_socket->readAll();
    // Parse JSON objects by bracket matching (no newline separator)
    while (true) {
        int start = m_buf.indexOf('{');
        if (start < 0) { m_buf.clear(); break; }
        m_buf.remove(0, start);

        int depth = 0, end = -1;
        for (int i = 0; i < m_buf.size(); i++) {
            if (m_buf.at(i) == '{') depth++;
            else if (m_buf.at(i) == '}') {
                depth--;
                if (depth == 0) { end = i + 1; break; }
            }
        }
        if (end < 0) break;

        QByteArray obj = m_buf.left(end).trimmed();
        m_buf.remove(0, end);
        if (obj.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument::fromJson(obj, &err);
        if (err.error == QJsonParseError::NoError)
            emit dataReceived(obj);
    }
}

void TcpClient::onError(QAbstractSocket::SocketError)
{
    emit errorOccurred(m_socket->errorString());
    if (!m_intentional && !m_host.isEmpty())
        m_reconnectTimer->start();
}

// ---- LAN scan (blocking, run in thread) ----
QStringList TcpClient::scanNetwork(int port, int timeoutMs)
{
    QStringList found;
    QStringList subnets;

    for (const auto &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol &&
            !addr.isLoopback() &&
            !addr.toString().startsWith("169.254.") &&
            !addr.toString().startsWith("198.18.") &&
            !addr.toString().startsWith("198.19."))
        {
            QString ip = addr.toString();
            int lastDot = ip.lastIndexOf('.');
            if (lastDot > 0)
                subnets.append(ip.left(lastDot + 1));
        }
    }
    if (subnets.isEmpty())
        subnets << "192.168.1." << "192.168.0.";

    for (const auto &base : subnets) {
        for (int i = 1; i < 255 && found.size() < 3; i++) {
            QString ip = base + QString::number(i);
            QTcpSocket sock;
            sock.connectToHost(ip, port);
            if (sock.waitForConnected(timeoutMs)) {
                if (!found.contains(ip))
                    found.append(ip);
                sock.disconnectFromHost();
            }
        }
    }
    return found;
}
