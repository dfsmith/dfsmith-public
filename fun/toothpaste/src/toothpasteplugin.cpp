#include "toothpaste.h"
#include <QStylePlugin>

// Plugin factory — lets Qt load toothpaste as a named style at runtime:
//   QApplication::setStyle("toothpaste")
//   or: QT_STYLE_OVERRIDE=toothpaste ./my_app
class ToothpastePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface"
                      FILE "toothpaste.json")
public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("toothpaste"), Qt::CaseInsensitive) == 0)
            return new ToothpasteStyle();
        return nullptr;
    }
};

// Required for Q_OBJECT defined inside a .cpp (not a header)
#include "toothpasteplugin.moc"
