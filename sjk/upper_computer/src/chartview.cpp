#include "chartview.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QtMath>

namespace {
void styleAxis(QAbstractAxis *axis)
{
    axis->setLabelsColor(QColor("#94a3b8"));
    axis->setTitleBrush(QBrush(QColor("#94a3b8")));
    axis->setGridLineColor(QColor("#334155"));
    axis->setLinePenColor(QColor("#475569"));
}

QPen seriesPen(const QColor &color)
{
    QPen pen(color);
    pen.setWidthF(2.0);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    return pen;
}
}

ChartView::ChartView(QWidget *parent) : QWidget(parent)
{
    m_tempSeries = new QLineSeries;
    m_humiSeries = new QLineSeries;
    m_lightSeries = new QLineSeries;

    m_tempSeries->setName("温度 °C（左轴）");
    m_humiSeries->setName("湿度 %RH（右轴）");
    m_lightSeries->setName("光照 %（右轴）");
    m_tempSeries->setPen(seriesPen(QColor("#f97316")));
    m_humiSeries->setPen(seriesPen(QColor("#06b6d4")));
    m_lightSeries->setPen(seriesPen(QColor("#eab308")));

    m_chart = new QChart;
    m_chart->addSeries(m_tempSeries);
    m_chart->addSeries(m_humiSeries);
    m_chart->addSeries(m_lightSeries);
    m_chart->setBackgroundBrush(QBrush(QColor("#1e293b")));
    m_chart->setPlotAreaBackgroundBrush(QBrush(QColor("#1e293b")));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setMargins(QMargins(8, 4, 8, 4));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setLabelColor(QColor("#94a3b8"));
    m_chart->legend()->setAlignment(Qt::AlignTop);

    m_axisX = new QDateTimeAxis;
    m_axisX->setFormat("HH:mm:ss");
    m_axisX->setTickCount(3);
    styleAxis(m_axisX);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_tempAxis = new QValueAxis;
    m_tempAxis->setTitleText("温度 °C");
    m_tempAxis->setRange(0, 50);
    m_tempAxis->setTickCount(6);
    m_tempAxis->setLabelFormat("%.0f");
    styleAxis(m_tempAxis);
    m_chart->addAxis(m_tempAxis, Qt::AlignLeft);

    m_percentAxis = new QValueAxis;
    m_percentAxis->setTitleText("湿度 / 光照 %");
    m_percentAxis->setRange(0, 100);
    m_percentAxis->setTickCount(6);
    m_percentAxis->setLabelFormat("%.0f");
    m_percentAxis->setGridLineVisible(false);
    styleAxis(m_percentAxis);
    m_chart->addAxis(m_percentAxis, Qt::AlignRight);

    m_tempSeries->attachAxis(m_axisX);
    m_tempSeries->attachAxis(m_tempAxis);
    m_humiSeries->attachAxis(m_axisX);
    m_humiSeries->attachAxis(m_percentAxis);
    m_lightSeries->attachAxis(m_axisX);
    m_lightSeries->attachAxis(m_percentAxis);

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setStyleSheet("background:#1e293b; border:none;");

    m_emptyLabel = new QLabel("等待传感器数据...");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyLabel->setStyleSheet(
        "color:#94a3b8; font-size:14px; background:transparent;");

    auto *chartStack = new QStackedLayout;
    chartStack->setStackingMode(QStackedLayout::StackAll);
    chartStack->addWidget(m_chartView);
    chartStack->addWidget(m_emptyLabel);

    auto *title = new QLabel("实时传感器曲线");
    title->setStyleSheet("color:#e2e8f0; font-size:16px; font-weight:600;");

    m_pauseButton = new QPushButton("暂停");
    auto *clearButton = new QPushButton("清空");
    const QString buttonStyle =
        "QPushButton { padding:6px 14px; border-radius:6px; border:none;"
        "background:#475569; color:#fff; font-size:12px; font-weight:600; }"
        "QPushButton:hover { background:#64748b; }";
    m_pauseButton->setStyleSheet(buttonStyle);
    clearButton->setStyleSheet(buttonStyle);
    connect(m_pauseButton, &QPushButton::clicked, this, &ChartView::togglePaused);
    connect(clearButton, &QPushButton::clicked, this, &ChartView::clearData);

    auto *header = new QHBoxLayout;
    header->addWidget(title);
    header->addStretch();
    header->addWidget(m_pauseButton);
    header->addWidget(clearButton);

    auto *panel = new QWidget;
    panel->setStyleSheet(
        "background:#1e293b; border:1px solid #334155; border-radius:12px;");
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(16, 12, 16, 12);
    panelLayout->addLayout(header);
    panelLayout->addLayout(chartStack, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 0, 20, 16);
    layout->addWidget(panel);
    updateEmptyState();
}

void ChartView::appendData(double temp, double humi, int light)
{
    if (m_paused)
        return;

    const qreal timestamp = QDateTime::currentMSecsSinceEpoch();
    m_tempSeries->append(timestamp, temp);
    m_humiSeries->append(timestamp, humi);
    m_lightSeries->append(timestamp, light);

    while (m_tempSeries->count() > MAX_POINTS)
        m_tempSeries->removePoints(0, 1);
    while (m_humiSeries->count() > MAX_POINTS)
        m_humiSeries->removePoints(0, 1);
    while (m_lightSeries->count() > MAX_POINTS)
        m_lightSeries->removePoints(0, 1);

    updateAxes();
    updateEmptyState();
}

void ChartView::togglePaused()
{
    m_paused = !m_paused;
    m_pauseButton->setText(m_paused ? "继续" : "暂停");
}

void ChartView::clearData()
{
    m_tempSeries->clear();
    m_humiSeries->clear();
    m_lightSeries->clear();
    m_tempAxis->setRange(0, 50);
    updateEmptyState();
}

void ChartView::updateAxes()
{
    const auto points = m_tempSeries->points();
    if (points.isEmpty())
        return;

    qreal minTemp = points.first().y();
    qreal maxTemp = points.first().y();
    for (const QPointF &point : points) {
        minTemp = qMin(minTemp, point.y());
        maxTemp = qMax(maxTemp, point.y());
    }
    if (qFuzzyCompare(minTemp, maxTemp)) {
        minTemp -= 5.0;
        maxTemp += 5.0;
    }
    const qreal padding = qMax<qreal>(2.0, (maxTemp - minTemp) * 0.2);
    m_tempAxis->setRange(qFloor(minTemp - padding), qCeil(maxTemp + padding));

    const qint64 firstTimestamp = qRound64(points.first().x());
    const qint64 lastTimestamp = qRound64(points.last().x());
    if (firstTimestamp == lastTimestamp) {
        m_axisX->setRange(
            QDateTime::fromMSecsSinceEpoch(firstTimestamp - 30000),
            QDateTime::fromMSecsSinceEpoch(lastTimestamp + 30000));
    } else {
        m_axisX->setRange(
            QDateTime::fromMSecsSinceEpoch(firstTimestamp),
            QDateTime::fromMSecsSinceEpoch(lastTimestamp));
    }
}

void ChartView::updateEmptyState()
{
    m_emptyLabel->setVisible(m_tempSeries->count() == 0);
}
