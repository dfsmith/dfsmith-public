#pragma once

#include <QProxyStyle>
#include <QList>
#include <QPointer>
#include <QTimer>

class QProgressBar;
class QStyleOptionProgressBar;

// ToothpasteStyle — a QProgressBar style that animates a toothpaste tube
// squeezing a wriggly white line across the bar as progress advances.
//
// The tube sits on the left; its nozzle points right.  The paste (a sine-wave
// line) emerges from the nozzle and grows rightward in proportion to the
// current progress value.  A shared timer drives the wriggle animation at ~60 fps.
class ToothpasteStyle : public QProxyStyle
{
    Q_OBJECT

public:
    explicit ToothpasteStyle(QStyle *base = nullptr);

    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override;

    // Called by Qt when a widget adopts this style — used to register progress
    // bars for animation.
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;

private slots:
    void advanceAnimation();

private:
    // Top-level draw — handles the full CE_ProgressBar element.
    void drawBar(QPainter *p, const QStyleOptionProgressBar *opt) const;

    // Draw the dark trough that forms the bar's background.
    void drawGroove(QPainter *p, const QRect &grooveRect) const;

    // Draw the wriggly toothpaste line inside pasteRect.
    // centerY and grooveH drive the amplitude/thickness of the paste.
    void drawPaste(QPainter *p, const QRect &pasteRect,
                   double centerY, double grooveH) const;

    // Draw the toothpaste tube (body + nozzle) inside tubeRect.
    void drawTube(QPainter *p, const QRect &tubeRect) const;

    // Phase of the sine wave, in [0, 1).  Advanced by advanceAnimation().
    mutable double m_phase = 0.0;

    QTimer *m_timer;
    QList<QPointer<QWidget>> m_animated;
};
