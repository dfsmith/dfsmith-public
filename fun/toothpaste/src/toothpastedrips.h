#pragma once

#include <QWidget>
#include <QTimer>
#include <QList>
#include <QtMath>

class QProgressBar;

struct Drip {
    double originX;   // screen x where drip was born
    double originY;   // screen y where drip was born (bottom of bar)
    double tipY;      // current falling tip y in screen coords
    double vy;        // vertical velocity (px/tick), accelerates under gravity
    double wPhase;    // per-drip horizontal wiggle phase offset (radians)
    double tipSize;   // teardrop radius at tip, grows as drip falls
};

// DripsOverlay — a transparent window that draws toothpaste drips
// falling from a QProgressBar.  Paste-style wiggly trails + teardrop blobs.
class DripsOverlay : public QWidget
{
    Q_OBJECT

public:
    // bar:    the progress bar to spawn drips from
    // parent: leave null so it's an independent top-level window
    explicit DripsOverlay(QProgressBar *bar, QWidget *parent = nullptr);

    // Reposition/resize to cover the full primary screen. Call once after show().
    void reposition();

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;

private slots:
    void tick();

private:
    void spawnDrip();
    void drawDrip(QPainter &p, const Drip &d) const;

    QProgressBar *m_bar;
    QTimer       *m_timer;
    QList<Drip>   m_drips;
    int           m_tickCount    = 0;
    double        m_globalPhase  = 0.0;   // shared animation phase (radians)

    // --- tuneable constants ---
    static constexpr int    kMaxDrips      = 30;
    static constexpr int    kSpawnInterval = 10;   // ticks between new drips
    static constexpr double kWavelength    = 20.0; // pixels per wiggle cycle
    static constexpr double kAmplitude     = 5.5;  // horizontal wiggle (px)
    static constexpr double kLineWidth     = 7.5;  // paste stroke width (px)
    static constexpr double kGravity       = 0.07; // acceleration (px/tick²)
    static constexpr double kMaxTipSize    = 14.0; // max teardrop radius (px)
};
