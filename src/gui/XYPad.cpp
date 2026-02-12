#include "XYPad.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

XYPad::XYPad(QWidget *parent) : QWidget(parent) {
    setMinimumSize(150, 150);
}

float XYPad::radius() const {
    return std::min(width(), height()) / 2.0f - 2.0f;
}

QPointF XYPad::center() const {
    return QPointF(width() / 2.0, height() / 2.0);
}

void XYPad::setPosition(float x, float y) {
    float r = radius();
    m_stickOffset = QPointF(x * r, y * r);
    update();
}

void XYPad::updatePosition(const QPointF &mousePos) {
    QPointF delta = mousePos - center();
    float r = radius();
    float dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    if (dist > r) {
        delta *= (r / dist);
    }
    m_stickOffset = delta;
    emit positionChanged(delta.x() / r, delta.y() / r);
    update();
}

void XYPad::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        updatePosition(event->position());
    }
}

void XYPad::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        updatePosition(event->position());
    }
}

void XYPad::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
}

void XYPad::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPointF c = center();
    float r = radius();

    // Boundary circle
    p.setPen(QPen(Qt::gray, 1.5));
    p.setBrush(QColor(40, 40, 40));
    p.drawEllipse(c, r, r);

    // Crosshair
    p.setPen(QPen(QColor(80, 80, 80), 0.5));
    p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));

    // Knob
    constexpr float knobRadius = 12.0f;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 130, 240));
    p.drawEllipse(c + m_stickOffset, knobRadius, knobRadius);
}
