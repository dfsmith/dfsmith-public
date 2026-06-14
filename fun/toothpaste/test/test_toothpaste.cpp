#include <QApplication>
#include <QMainWindow>
#include <QProgressBar>
#include <QSlider>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "toothpaste.h"
#include "toothpastedrips.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(new ToothpasteStyle());

    QMainWindow window;
    window.setWindowTitle("Toothpaste Progress Bar — Test");
    window.resize(620, 240);

    auto *central = new QWidget();
    auto *vbox    = new QVBoxLayout(central);
    vbox->setSpacing(10);
    vbox->setContentsMargins(18, 14, 18, 14);

    // Manual bar — controlled by slider
    auto *bar1 = new QProgressBar();
    bar1->setRange(0, 100);
    bar1->setValue(35);
    bar1->setFixedHeight(52);
    bar1->setTextVisible(false);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(35);
    QObject::connect(slider, &QSlider::valueChanged, bar1, &QProgressBar::setValue);

    // Auto-running bar
    auto *bar2 = new QProgressBar();
    bar2->setRange(0, 100);
    bar2->setValue(0);
    bar2->setFixedHeight(52);
    bar2->setTextVisible(false);

    auto *autoTimer = new QTimer(&app);
    autoTimer->setInterval(55);
    QObject::connect(autoTimer, &QTimer::timeout, [bar2]() {
        bar2->setValue((bar2->value() + 1) % 101);
    });
    autoTimer->start();

    vbox->addWidget(new QLabel("Manual (drag slider):"));
    vbox->addWidget(bar1);
    vbox->addWidget(slider);
    vbox->addSpacing(6);
    vbox->addWidget(new QLabel("Auto-running:"));
    vbox->addWidget(bar2);
    vbox->addStretch();

    window.setCentralWidget(central);
    window.show();

    // Drips fall from bar1 (manual bar) and bar2 (auto bar)
    auto *drips1 = new DripsOverlay(bar1);
    auto *drips2 = new DripsOverlay(bar2);
    drips1->show();
    drips2->show();

    return app.exec();
}
