#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "tcpclient.h"
#include "dataparser.h"
#include "dashboard.h"
#include "chartview.h"
#include "datastore.h"
#include "devicepanel.h"
#include "databridgeserver.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void connectToDevice(const QString &host);

private slots:
    void onTcpData(const QByteArray &data);
    void onTcpConnected(const QString &host);
    void onTcpDisconnected();
    void onConnectRequested(const QString &host);
    void onScanRequested();
    void onDisconnectRequested();

private:
    TcpClient   *m_tcp;
    DataParser  *m_parser;
    Dashboard   *m_dashboard;
    ChartView   *m_chartView;
    DataStore   *m_store;
    DevicePanel *m_panel;
    DataBridgeServer *m_bridge;
};

#endif
