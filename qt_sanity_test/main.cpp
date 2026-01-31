#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("DracoVed - Qt6 Sanity Test");
    window.resize(420, 160);

    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* label = new QLabel(
        "Qt Widgets is working.\n"
        "This window will auto-close in 2 seconds.",
        &window
    );
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* button = new QPushButton("Close now", &window);
    layout->addWidget(button);

    QObject::connect(button, &QPushButton::clicked, &app, &QApplication::quit);
    QTimer::singleShot(2000, &app, &QApplication::quit);

    window.show();
    return app.exec();
}

