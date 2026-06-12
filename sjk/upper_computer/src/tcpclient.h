#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class TcpClient : public QObject
{
    Q_OBJECT
public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();

    void connectToHost(const QString &host, quint16 port = 25576);
    void disconnect();
    bool isConnected() const;
    bool isIntentionalDisconnect() const;
    QString host() const;

    // Scan LAN for ESP8266 devices.
    static QStringList scanNetwork(int port = 25576, int timeoutMs = 400);

signals:
    void connected(const QString &host);
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &errMsg);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError);

private:
    QTcpSocket *m_socket;
    QTimer     *m_reconnectTimer;
    QString     m_host;
    quint16     m_port;
    QByteArray  m_buf;
    bool        m_intentional = false;
};

#endif
