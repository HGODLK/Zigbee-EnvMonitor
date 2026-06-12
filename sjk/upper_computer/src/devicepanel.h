#ifndef DEVICEPANEL_H
#define DEVICEPANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>

class DevicePanel : public QWidget
{
    Q_OBJECT
public:
    explicit DevicePanel(QWidget *parent = nullptr);
    void setScanning(bool scanning);
    void addScanResult(const QString &ip);
    void clearScanResults();

signals:
    void connectRequested(const QString &host);
    void scanRequested();
    void disconnectRequested();

private:
    QLineEdit   *m_ipInput;
    QPushButton *m_btnConnect;
    QPushButton *m_btnScan;
    QPushButton *m_btnDisconnect;
    QListWidget *m_scanList;
    QLabel      *m_statusLabel;
};

#endif
