#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("iPop");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("iPop");

    // Configurar estilo base Fusion y paleta clásica de iTunes 10 (Platino / Metal cepillado)
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette itunesPalette;
    itunesPalette.setColor(QPalette::Window, QColor(234, 236, 240));         // #EAECF0 Base metálica platino
    itunesPalette.setColor(QPalette::WindowText, QColor(32, 36, 42));       // #20242A Texto principal oscuro
    itunesPalette.setColor(QPalette::Base, QColor(255, 255, 255));           // #FFFFFF Fondo tabla
    itunesPalette.setColor(QPalette::AlternateBase, QColor(242, 246, 252));  // #F2F6FC Filas alternadas cian/azul suave (Zebra)
    itunesPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    itunesPalette.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
    itunesPalette.setColor(QPalette::Text, QColor(32, 36, 42));
    itunesPalette.setColor(QPalette::Button, QColor(238, 240, 244));
    itunesPalette.setColor(QPalette::ButtonText, QColor(32, 36, 42));
    itunesPalette.setColor(QPalette::BrightText, Qt::red);
    itunesPalette.setColor(QPalette::Link, QColor(36, 102, 212));
    itunesPalette.setColor(QPalette::Highlight, QColor(56, 117, 215));       // #3875D7 Azul Real icónico de iTunes
    itunesPalette.setColor(QPalette::HighlightedText, Qt::white);
    itunesPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 146, 154));
    itunesPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 146, 154));
    app.setPalette(itunesPalette);

    MainWindow window;
    window.show();

    return app.exec();
}
