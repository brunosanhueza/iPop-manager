#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("iPop");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("iPop");

    // Configurar estilo base Fusion y paleta moderna oscura
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(15, 23, 42));       // Slate 900
    darkPalette.setColor(QPalette::WindowText, QColor(241, 245, 249));  // Slate 100
    darkPalette.setColor(QPalette::Base, QColor(30, 41, 59));         // Slate 800
    darkPalette.setColor(QPalette::AlternateBase, QColor(15, 23, 42));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(15, 23, 42));
    darkPalette.setColor(QPalette::ToolTipText, QColor(241, 245, 249));
    darkPalette.setColor(QPalette::Text, QColor(241, 245, 249));
    darkPalette.setColor(QPalette::Button, QColor(30, 41, 59));
    darkPalette.setColor(QPalette::ButtonText, QColor(241, 245, 249));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(56, 189, 248));        // Sky 400
    darkPalette.setColor(QPalette::Highlight, QColor(37, 99, 235));    // Blue 600
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(100, 116, 139));
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 116, 139));
    app.setPalette(darkPalette);

    MainWindow window;
    window.show();

    return app.exec();
}
