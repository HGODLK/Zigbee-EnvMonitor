#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QDateTime>
#include <QLabel>
#include <QPushButton>

class ChartView : public QWidget
{
    Q_OBJECT
public:
    explicit ChartView(QWidget *parent = nullptr);
    void appendData(double temp, double humi, int light);

private slots:
    void togglePaused();
    void clearData();

private:
    void updateAxes();
    void updateEmptyState();

    QChart *m_chart;
    QChartView *m_chartView;
    QLineSeries *m_tempSeries;
    QLineSeries *m_humiSeries;
    QLineSeries *m_lightSeries;
    QDateTimeAxis *m_axisX;
    QValueAxis *m_tempAxis;
    QValueAxis *m_percentAxis;
    QLabel *m_emptyLabel;
    QPushButton *m_pauseButton;
    bool m_paused = false;
    static constexpr int MAX_POINTS = 60;
};

#endif
