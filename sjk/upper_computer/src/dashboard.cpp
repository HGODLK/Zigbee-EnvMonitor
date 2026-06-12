#include "dashboard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>

Dashboard::Dashboard(QWidget *parent) : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 16, 20, 16);

    // ---- 状态栏 ----
    auto *statusLayout = new QHBoxLayout();
    m_statusIcon = new QLabel("●");
    m_statusIcon->setStyleSheet("color:#ef4444; font-size:14px;");
    m_statusText = new QLabel("未连接");
    m_statusText->setStyleSheet("color:#94a3b8; font-size:14px;");
    statusLayout->addWidget(m_statusIcon);
    statusLayout->addWidget(m_statusText);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // ---- 传感器卡片 ----
    auto *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(16);

    auto makeCard = [this](const QString &label, const QString &unit,
                           QLabel *&val, const QString &color) -> QWidget* {
        auto *card = new QWidget;
        card->setStyleSheet(
            "background:#1e293b; border:1px solid #334155; border-radius:12px;");
        auto *lay = new QVBoxLayout(card);
        lay->setAlignment(Qt::AlignCenter);

        auto *lbl = new QLabel(label);
        lbl->setStyleSheet("color:#94a3b8; font-size:12px; letter-spacing:1px; border:none;");
        lbl->setAlignment(Qt::AlignCenter);
        lay->addWidget(lbl);

        val = new QLabel("--");
        val->setAlignment(Qt::AlignCenter);
        val->setStyleSheet(
            QString("color:%1; font-size:52px; font-weight:bold; border:none;").arg(color));
        lay->addWidget(val);

        auto *u = new QLabel(unit);
        u->setStyleSheet("color:#94a3b8; font-size:14px; border:none;");
        u->setAlignment(Qt::AlignCenter);
        lay->addWidget(u);

        return card;
    };

    cardsLayout->addWidget(makeCard("🌡  温度", "°C",  m_tempVal,  "#f97316"));
    cardsLayout->addWidget(makeCard("💧 湿度", "%RH", m_humiVal,  "#06b6d4"));
    cardsLayout->addWidget(makeCard("☀  光照", "%",   m_lightVal, "#eab308"));

    mainLayout->addLayout(cardsLayout);
}

void Dashboard::updateData(const SensorData &data)
{
    if (data.valid) {
        m_tempVal->setText(QString::number(data.temp, 'f', 1));
        m_humiVal->setText(QString::number(data.humi, 'f', 1));
        m_lightVal->setText(QString::number(data.light));
    }
}

void Dashboard::setConnected(bool ok, const QString &host)
{
    if (ok) {
        m_statusIcon->setStyleSheet("color:#22c55e; font-size:14px;");
        m_statusText->setText(QString("已连接 %1").arg(host));
        m_statusText->setStyleSheet("color:#22c55e; font-size:14px;");
    } else {
        m_statusIcon->setStyleSheet("color:#ef4444; font-size:14px;");
        m_statusText->setText("未连接");
        m_statusText->setStyleSheet("color:#94a3b8; font-size:14px;");
    }
}
