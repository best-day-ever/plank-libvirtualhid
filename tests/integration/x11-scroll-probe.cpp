/** @file x11-scroll-probe.cpp @brief Native Qt receipt of synthetic qualification input. */
#include <cstdio>
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

/** @brief Record application wheel deltas while preserving default widget handling. */
class Probe final: public QObject {
public:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::Wheel) {
      const auto *wheel = static_cast<QWheelEvent *>(event);
      std::printf("wheel %d %d modifiers=%d\n", wheel->angleDelta().x(), wheel->angleDelta().y(), int(wheel->modifiers()));
      std::fflush(stdout);
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      std::printf("button %d %d\n", int(event->type()), int(mouse->button()));
      std::fflush(stdout);
    }
    return false;
  }
};

/** @brief Run on the caller's isolated X11 display. */
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QScrollArea window;
  auto *content = new QLabel("Wheel qualification");
  content->setFixedSize(5000, 5000);
  window.setWidget(content);
  window.resize(800, 600);
  Probe probe;
  window.viewport()->installEventFilter(&probe);
  window.show();
  window.horizontalScrollBar()->setValue(2000);
  window.verticalScrollBar()->setValue(2000);
  return app.exec();
}
