#include "toothpaste.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QStyleOptionProgressBar>
#include <QtMath>

// --- tuneable constants ------------------------------------------------------

static constexpr double kWavelengthFactor =
    3.5;  // wavelength = grooveH * factor
static constexpr double kAmplitudeFactor =
    0.2;  // amplitude  = grooveH * factor (>1 leaks outside groove)
static constexpr double kPhaseStep = 0.0;    // phase units per timer tick
static constexpr int kTimerIntervalMs = 16;  // ~60 fps

// -----------------------------------------------------------------------------

ToothpasteStyle::ToothpasteStyle(QStyle *base)
    : QProxyStyle(base), m_timer(new QTimer(this)) {
  m_timer->setInterval(kTimerIntervalMs);
  connect(m_timer, &QTimer::timeout, this, &ToothpasteStyle::advanceAnimation);
}

void ToothpasteStyle::advanceAnimation() {
  m_phase += kPhaseStep;
  if (m_phase >= 1.0) m_phase -= 1.0;

  for (const QPointer<QWidget> &wp : qAsConst(m_animated))
    if (wp) wp->update();
}

void ToothpasteStyle::polish(QWidget *widget) {
  QProxyStyle::polish(widget);
  if (qobject_cast<QProgressBar *>(widget)) {
    const QPointer<QWidget> wp(widget);
    if (!m_animated.contains(wp)) m_animated.append(wp);
    if (!m_timer->isActive()) m_timer->start();
  }
}

void ToothpasteStyle::unpolish(QWidget *widget) {
  m_animated.removeOne(QPointer<QWidget>(widget));
  if (m_animated.isEmpty()) m_timer->stop();
  QProxyStyle::unpolish(widget);
}

// --- drawControl -------------------------------------------------------------

void ToothpasteStyle::drawControl(ControlElement element,
                                  const QStyleOption *option, QPainter *p,
                                  const QWidget *widget) const {
  if (element == CE_ProgressBar) {
    const auto *opt =
        qstyleoption_cast<const QStyleOptionProgressBar *>(option);
    if (opt) {
      drawBar(p, opt);
      return;
    }
  }

  // Sub-elements (groove, contents, label) are all handled inside drawBar().
  // If something calls them individually, swallow to avoid partial redraws.
  if (element == CE_ProgressBarGroove || element == CE_ProgressBarContents ||
      element == CE_ProgressBarLabel)
    return;

  QProxyStyle::drawControl(element, option, p, widget);
}

// --- private drawing helpers -------------------------------------------------

void ToothpasteStyle::drawBar(QPainter *p,
                              const QStyleOptionProgressBar *opt) const {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);

  const QRect r = opt->rect;
  const int rh = r.height();

  // Tube on the left: wide enough to look like a real tube but never > 1/5 of
  // bar
  const int tubeW = qMin(int(rh * 2.6), r.width() / 5);
  const int grooveH = qMax(4, int(rh * 0.52));
  const int grooveCY = r.center().y();

  const QRect grooveRect(r.left(), grooveCY - grooveH / 2, r.width(), grooveH);
  const QRect tubeRect(r.left(), r.top(), tubeW, rh);

  // 1. Groove background (full width)
  drawGroove(p, grooveRect);

  // 2. Toothpaste paste, clipped to the filled area
  const int total = opt->maximum - opt->minimum;
  const double fraction =
      (total > 0)
          ? qBound(0.0, double(opt->progress - opt->minimum) / double(total),
                   1.0)
          : 0.0;

  const int pasteAreaLeft = r.left() + tubeW;
  const int pasteAreaWidth = r.width() - tubeW;
  const int pasteRight = pasteAreaLeft + int(pasteAreaWidth * fraction);

  if (pasteRight > pasteAreaLeft) {
    const QRect pasteRect(pasteAreaLeft, r.top(), pasteRight - pasteAreaLeft,
                          rh);
    drawPaste(p, pasteRect, grooveCY, grooveH);
  }

  // 3. Tube overlaid on left (drawn last so it sits on top of paste/groove)
  drawTube(p, tubeRect);

  p->restore();
}

void ToothpasteStyle::drawGroove(QPainter *p, const QRect &r) const {
  // Dark navy trough with a subtle inner edge highlight
  p->setPen(QPen(QColor(10, 10, 35), 1));
  p->setBrush(QColor(22, 22, 55));
  p->drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);

  p->setPen(QPen(QColor(60, 60, 100, 70), 1));
  p->setBrush(Qt::NoBrush);
  p->drawRoundedRect(r.adjusted(1, 1, -1, -1), r.height() / 2.0,
                     r.height() / 2.0);
}

void ToothpasteStyle::drawPaste(QPainter *p, const QRect &pasteRect,
                                double centerY, double grooveH) const {
  const double amplitude = grooveH * kAmplitudeFactor;
  const double wavelength = grooveH * kWavelengthFactor;
  const double lineWidth = grooveH * 0.25;

  // Build the wriggly sine path.  We extend one extra wavelength beyond the
  // clip rect so the wave never ends with a flat segment at the progress tip.
  QPainterPath path;
  const double xStart = pasteRect.left();
  const double xEnd = pasteRect.right() + wavelength;

  for (double x = xStart; x <= xEnd; x += 0.8) {
    const double angle = 2.0 * M_PI * ((x - xStart) / wavelength + m_phase);
    const double y = centerY + amplitude * qSin(angle);
    if (x <= xStart + 0.1)
      path.moveTo(x, y);
    else
      path.lineTo(x, y);
  }

  // Clip only on X (respect progress amount) — leave Y unconstrained so the
  // wriggly paste can leak above and below the groove / widget bounds.
  const QRect xOnlyClip(pasteRect.left(), -32767, pasteRect.width(), 65535);
  p->setClipRect(xOnlyClip, Qt::ReplaceClip);

  // White paste body
  p->setPen(QPen(QColor(245, 245, 252), lineWidth, Qt::SolidLine, Qt::RoundCap,
                 Qt::RoundJoin));
  p->drawPath(path);

  // Classic toothpaste blue stripe running along the centre
  p->setPen(QPen(QColor(80, 140, 230, 210), lineWidth * 0.20, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin));
  p->drawPath(path);

  // Specular highlight: same path shifted slightly upward
  QTransform shiftUp;
  shiftUp.translate(0, -lineWidth * 0.22);
  p->setPen(QPen(QColor(255, 255, 255, 155), lineWidth * 0.18, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin));
  p->drawPath(shiftUp.map(path));

  p->setClipping(false);
}

void ToothpasteStyle::drawTube(QPainter *p, const QRect &r) const {
  const double x = r.left();
  const double y = r.top();
  const double w = r.width();
  const double h = r.height();
  const double cy = r.center().y();

  // The rightmost sliver is the tapering nozzle; the rest is the tube body.
  const double bodyW = w * 0.9;
  const double bodyH = bodyW * 0.3;
  const double bodyRadius = h * 0.05;
  const double nozzleW = w * 0.1;
  const double nozzleH = bodyH * 0.40;

  // ---- Tube body: red 3-D cylinder look via vertical gradient ----
  QLinearGradient vGrad(0, y, 0, y + h);
  vGrad.setColorAt(0.00, QColor(150, 15, 15));
  vGrad.setColorAt(0.30, QColor(225, 55, 55));
  vGrad.setColorAt(0.50, QColor(255, 90, 80));  // top-lit highlight
  vGrad.setColorAt(0.70, QColor(215, 45, 45));
  vGrad.setColorAt(1.00, QColor(130, 10, 10));

  p->setBrush(vGrad);
  p->setPen(QPen(QColor(100, 8, 8), 0.8));
  p->drawRoundedRect(QRectF(x, cy - bodyH / 2, bodyW, bodyH), bodyRadius,
                     bodyRadius);

  // White label area (where brand name would go)
  p->setBrush(QColor(252, 252, 252, 210));
  p->setPen(Qt::NoPen);
  p->drawRoundedRect(
      QRectF(x + bodyW * 0.25, cy - bodyH * 0.22, bodyW * 0.55, bodyH * 0.44),
      2.0, 2.0);

  // Decorative blue stripes top & bottom
  const double stripeH = h * 0.12;
  const double stripeW = bodyW * 0.65;
  const double stripeX = x + bodyW * 0.17;
  p->setBrush(QColor(35, 95, 210));
  p->drawRoundedRect(QRectF(stripeX, y + h * 0.11, stripeW, stripeH), 1.5, 1.5);
  p->drawRoundedRect(QRectF(stripeX, y + h * 0.77, stripeW, stripeH), 1.5, 1.5);

  // ---- Nozzle: silver trapezoid tapering to the right ----
  const double nozzleX = x + bodyW;

  QPolygonF nozzle;
  nozzle << QPointF(nozzleX, cy - nozzleH * 0.6)
         << QPointF(nozzleX + nozzleW, cy - nozzleH * 0.5)
         << QPointF(nozzleX + nozzleW, cy + nozzleH * 0.5)
         << QPointF(nozzleX, cy + nozzleH * 0.6);

  QLinearGradient nozzGrad(nozzleX, 0, nozzleX + nozzleW, 0);
  nozzGrad.setColorAt(0.0, QColor(195, 195, 210));
  nozzGrad.setColorAt(1.0, QColor(155, 155, 170));

  p->setBrush(nozzGrad);
  p->setPen(QPen(QColor(100, 100, 120), 0.6));
  p->drawPolygon(nozzle);

  // Small circle at the nozzle tip — the opening where paste emerges
  const double dotR = nozzleH * 0.18;
  p->setBrush(QColor(235, 240, 255));
  p->setPen(QPen(QColor(160, 165, 180), 0.5));
  p->drawEllipse(QPointF(nozzleX + nozzleW, cy), dotR, dotR);
}
