#include "PcbCanvas.h"

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {}

void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
}
