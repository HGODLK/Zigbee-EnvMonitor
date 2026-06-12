#include "mainwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("环境数据采集系统");
    resize(1100, 700);
    setStyleSheet("background:#0f172a;");

    // ---- Central widget ----
    auto *central = new QWidget;
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left panel (device)
    m_panel = new DevicePanel;
    m_panel->setFixedWidth(240);

    // Right area: dashboard + chart
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_dashboard = new Dashboard;
    rightLayout->addWidget(m_dashboard);

    m_chartView = new ChartView;
    rightLayout->addWidget(m_chartView, 1);

    // Splitter
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_panel);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter);

    setCentralWidget(central);

    // ---- Modules ----
    m_tcp    = new TcpClient(this);
    m_parser = new DataParser(this);
    m_store  = new DataStore(this);
    m_bridge = new DataBridgeServer(this);
    if (!m_bridge->start(25577)) {
        QMessageBox::warning(
            this, "数据共享服务",
            QString("无法启动 25577 数据共享端口：%1").arg(m_bridge->errorString()));
    }

    // ---- Connections ----
    // TCP data → parser
    connect(m_tcp, &TcpClient::dataReceived, this, &MainWindow::onTcpData);
    connect(m_tcp, &TcpClient::connected,    this, &MainWindow::onTcpConnected);
    connect(m_tcp, &TcpClient::disconnected, this, &MainWindow::onTcpDisconnected);
    connect(m_tcp, &TcpClient::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(QString("设备连接错误：%1").arg(message));
        m_bridge->publishStatus(
            false, m_tcp->host(), "设备连接错误", message);
    });

    // Device panel
    connect(m_panel, &DevicePanel::connectRequested,
            this, &MainWindow::onConnectRequested);
    connect(m_panel, &DevicePanel::scanRequested,
            this, &MainWindow::onScanRequested);
    connect(m_panel, &DevicePanel::disconnectRequested,
            this, &MainWindow::onDisconnectRequested);

    // Status bar
    statusBar()->setStyleSheet(
        "background:#1e293b; color:#94a3b8; font-size:12px; border-top:1px solid #334155;");
    statusBar()->showMessage("就绪 — 请连接设备；Web 数据端口 25577");
    m_bridge->publishStatus(false, "", "设备未连接");
}

MainWindow::~MainWindow()
{
    m_store->stopRecording();
}

void MainWindow::connectToDevice(const QString &host)
{
    onConnectRequested(host);
}

void MainWindow::onConnectRequested(const QString &host)
{
    statusBar()->showMessage(QString("正在连接设备 %1:25576...").arg(host));
    m_bridge->publishStatus(false, host, "正在连接设备");
    m_tcp->connectToHost(host, 25576);
    m_panel->setScanning(false);
}

void MainWindow::onScanRequested()
{
    m_panel->clearScanResults();
    m_panel->setScanning(true);
    statusBar()->showMessage("正在扫描局域网设备...");

    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher]() {
        QStringList found = watcher->result();
        m_panel->setScanning(false);
        if (found.isEmpty()) {
            statusBar()->showMessage("扫描完成 — 未找到设备，请手动输入 IP");
        } else {
            statusBar()->showMessage(QString("扫描完成 — 发现 %1 个设备").arg(found.size()));
            for (const auto &ip : found) {
                m_panel->addScanResult(ip);
            }
            // Auto-connect to first
            onConnectRequested(found.first());
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(TcpClient::scanNetwork, 25576, 400));
}

void MainWindow::onDisconnectRequested()
{
    m_tcp->disconnect();
    statusBar()->showMessage("已断开");
    m_bridge->publishStatus(false, "", "已手动断开设备");
}

void MainWindow::onTcpConnected(const QString &host)
{
    m_dashboard->setConnected(true, host);
    statusBar()->showMessage(QString("已连接 %1:25576 — 接收数据中").arg(host));
    if (!m_store->isRecording())
        m_store->startRecording();
    m_bridge->publishStatus(true, host, QString("已连接 %1:25576").arg(host));
}

void MainWindow::onTcpDisconnected()
{
    m_dashboard->setConnected(false);
    m_store->stopRecording();
    const QString message = m_tcp->isIntentionalDisconnect()
        ? QString("设备已断开")
        : QString("设备连接断开，正在自动重连");
    statusBar()->showMessage(message);
    m_bridge->publishStatus(false, m_tcp->host(), message);
}

void MainWindow::onTcpData(const QByteArray &data)
{
    SensorData sd = DataParser::parseJson(data);
    if (sd.valid) {
        m_dashboard->updateData(sd);
        m_chartView->appendData(sd.temp, sd.humi, sd.light);
        m_store->writeRow(sd);
        m_bridge->publishSensor(sd);
    }
}
