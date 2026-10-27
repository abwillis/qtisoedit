/*
 * main.cpp
 * bkiso-qt: Qt front end for the bkisofs ISO 9660 library.
 */
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bkiso-qt"));
    QApplication::setOrganizationName(QStringLiteral("bkiso-qt"));
    QApplication::setApplicationVersion(QStringLiteral(BKISO_QT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Edit ISO 9660 images with bkisofs."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("image"),
                                 QObject::tr("ISO or NRG image to open."),
                                 QStringLiteral("[image]"));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        const QString path = args.first();
        QTimer::singleShot(0, &window, [&window, path] { window.openImage(path); });
    }

    return app.exec();
}
