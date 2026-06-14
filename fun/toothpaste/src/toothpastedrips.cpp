#include "toothpastedrips.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QRandomGenerator>
#include <QScreen>
#include <algorithm>

DripsOverlay::DripsOverlay(QProgressBar *bar, QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint
            | Qt::WindowStaysOnTopHint
            | Qt::Tool
            | Qt::WindowTransparentForInput)   // mouse clicks pass through
    , m_bar(bar)
    , m_timer(new QTimer(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    m_timer->setInterval(16);  // ~60 fps
    connect(m_timer, &QTimer::timeout, this, &DripsOverlay::tick);
}

void DripsOverlay::reposition()
{
    if (!m_bar->isVisible()) return;

    // A square whose side = bar width, positioned directly below the bar.
    const int side      = m_bar->width();
    const QPoint origin = m_bar->mapToGlobal(QPoint(0, m_bar->height()));
    setGeometry(origin.x(), origin.y(), side, side);
    raise();
}

void DripsOverlay::showEvent(QShowEvent *ev)
{
    QWidget::showEvent(ev);
    reposition();
    m_timer->start();
}

// ---------------------------------------------------------------------------

void DripsOverlay::spawnDrip()
{
    if (m_drips.size() >= kMaxDrips) return;
    if (!m_bar->isVisible()) return;

    const int min = m_bar->minimum();
    const int max = m_bar->maximum();
    const int val = m_bar->value();
    if (max <= min || val <= min) return;

    // Replicate the tube-width formula from toothpaste.cpp so we know where
    // the paste actually lives.
    const double fraction = double(val - min) / double(max - min);
    const int barH        = m_bar->height();
    const int tubeW       = qMin(int(barH * 2.6), m_bar->width() / 5);
    const int pasteLeft   = tubeW;
    const int pasteWidth  = m_bar->width() - tubeW;
    const int pasteRight  = pasteLeft + int(pasteWidth * fraction);

    if (pasteRight <= pasteLeft) return;

    // Pick a random x within the paste region.
    // Convert to overlay-local coordinates (origin = top-left of this widget).
    const int localX  = pasteLeft +
        int(QRandomGenerator::global()->bounded(uint(pasteRight - pasteLeft)));
    const QPoint overlayPt = m_bar->mapTo(
        nullptr, QPoint(localX, m_bar->height()));  // screen coords
    const QPoint localPt   = mapFromGlobal(overlayPt);

    Drip d;
    d.originX = localPt.x();
    d.originY = 0.0;   // top of overlay == bottom of bar
    d.tipY    = 0.0;
    d.vy      = 0.8 + QRandomGenerator::global()->generateDouble() * 1.8;
    d.wPhase  = QRandomGenerator::global()->generateDouble() * 2.0 * M_PI;
    d.tipSize = 0.0;
    m_drips.append(d);
}

void DripsOverlay::tick()
{
    ++m_tickCount;
    m_globalPhase += 0.018 * 2.0 * M_PI / 1.0;
    if (m_globalPhase >= 2.0 * M_PI) m_globalPhase -= 2.0 * M_PI;

    if (m_tickCount % kSpawnInterval == 0)
        spawnDrip();

    const int overlayH = height();

    for (auto &d : m_drips) {
        d.tipY   += d.vy;
        d.vy     += kGravity;
        d.tipSize = qMin(d.tipSize + 0.18, kMaxTipSize);
    }

    // Cull drips that have fallen past the overlay's bottom edge
    m_drips.erase(
        std::remove_if(m_drips.begin(), m_drips.end(),
            [overlayH](const Drip &d){ return d.tipY > overlayH + 80; }),
        m_drips.end());

    update();
}

// ---------------------------------------------------------------------------

void DripsOverlay::drawDrip(QPainter &p, const Drip &d) const
{
    // Build a wiggly vertical path from the origin down to the current tip.
    QPainterPath trail;
    bool started = false;
    for (double y = d.originY; y <= d.tipY; y += 1.2) {
        const double xOff = kAmplitude *
            qSin(2.0 * M_PI * y / kWavelength + d.wPhase + m_globalPhase);
        if (!started) { trail.moveTo(d.originX + xOff, y); started = true; }
        else           trail.lineTo(d.originX + xOff, y);
    }

    if (!started) return;

    // White paste body
    p.setPen(QPen(QColor(245, 245, 252), kLineWidth,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(trail);

    // Blue centre stripe
    p.setPen(QPen(QColor(80, 140, 230, 200), kLineWidth * 0.20,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(trail);

    // Specular highlight — trail shifted slightly left
    QTransform shift;
    shift.translate(-kAmplitude * 0.25, 0);
    p.setPen(QPen(QColor(255, 255, 255, 130), kLineWidth * 0.18,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(shift.map(trail));

    // --- Teardrop blob at the falling tip ---
    if (d.tipSize > 0.5) {
        const double xTip = d.originX + kAmplitude *
            qSin(2.0 * M_PI * d.tipY / kWavelength + d.wPhase + m_globalPhase);
        const QPointF tip(xTip, d.tipY);
        const double rx = d.tipSize;
        const double ry = d.tipSize * 1.5;   // elongated teardrop

        p.setPen(Qt::NoPen);

        // White body
        p.setBrush(QColor(245, 245, 252));
        p.drawEllipse(tip, rx, ry);

        // Blue stripe band across blob
        p.setBrush(QColor(80, 140, 230, 170));
        p.drawEllipse(tip, rx * 0.28, ry * 0.28);

        // Specular catch-light
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawEllipse(QPointF(tip.x() - rx * 0.32, tip.y() - ry * 0.38),
                      rx * 0.22, ry * 0.16);
    }
}

void DripsOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    for (const Drip &d : qAsConst(m_drips))
        drawDrip(p, d);
}
