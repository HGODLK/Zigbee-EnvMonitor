#include "devicepanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

DevicePanel::DevicePanel(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    lay->setContentsMargins(16, 12, 16, 12);

    // Title
    auto *title = new QLabel("🔌 设备连接");
    title->setStyleSheet("color:#e2e8f0; font-size:15px; font-weight:600;");
    lay->addWidget(title);

    // IP input + connect
    auto *row1 = new QHBoxLayout();
    m_ipInput = new QLineEdit;
    m_ipInput->setPlaceholderText("ESP8266 IP 地址 (如 192.168.1.100)");
    m_ipInput->setStyleSheet(
        "padding:8px 12px; border-radius:6px; border:1px solid #334155;"
        "background:#0f172a; color:#e2e8f0; font-size:13px;");
    row1->addWidget(m_ipInput, 1);

    m_btnConnect = new QPushButton("连接");
    m_btnConnect->setStyleSheet(
        "padding:8px 18px; border-radius:6px; border:none;"
        "background:#22c55e; color:#fff; font-weight:600;");
    row1->addWidget(m_btnConnect);
    lay->addLayout(row1);

    // Scan + Disconnect
    auto *row2 = new QHBoxLayout();
    m_btnScan = new QPushButton("🔍 自动扫描");
    m_btnScan->setStyleSheet(
        "padding:8px 18px; border-radius:6px; border:none;"
        "background:#3b82f6; color:#fff; font-weight:600;");
    row2->addWidget(m_btnScan);

    m_btnDisconnect = new QPushButton("断开");
    m_btnDisconnect->setStyleSheet(
        "padding:8px 18px; border-radius:6px; border:none;"
        "background:#ef4444; color:#fff; font-weight:600;");
    row2->addWidget(m_btnDisconnect);
    row2->addStretch();
    lay->addLayout(row2);

    // Scan result list
    m_scanList = new QListWidget;
    m_scanList->setMaximumHeight(60);
    m_scanList->setStyleSheet(
        "background:#0f172a; border:1px solid #334155; border-radius:6px;"
        "color:#94a3b8; font-size:12px;");
    m_scanList->setVisible(false);
    lay->addWidget(m_scanList);

    // Status
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color:#94a3b8; font-size:11px;");
    lay->addWidget(m_statusLabel);

    lay->addStretch();

    // Signals
    connect(m_btnConnect, &QPushButton::clicked, this, [this]() {
        QString ip = m_ipInput->text().trimmed();
        if (!ip.isEmpty())
            emit connectRequested(ip);
    });
    connect(m_btnScan, &QPushButton::clicked, this, &DevicePanel::scanRequested);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &DevicePanel::disconnectRequested);
    connect(m_scanList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_ipInput->setText(item->text());
        emit connectRequested(item->text());
    });
}

void DevicePanel::setScanning(bool scanning)
{
    m_btnScan->setEnabled(!scanning);
    m_btnScan->setText(scanning ? "⏳ 扫描中..." : "🔍 自动扫描");
    m_statusLabel->setText(scanning ? "扫描中..." : "就绪");
    m_scanList->setVisible(!scanning);
}

void DevicePanel::addScanResult(const QString &ip)
{
    m_scanList->setVisible(true);
    m_scanList->addItem(ip);
}

void DevicePanel::clearScanResults()
{
    m_scanList->clear();
    m_scanList->setVisible(false);
}
