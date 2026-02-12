#ifndef XYPAD_H
#define XYPAD_H

#include <QWidget>
#include <QPointF>

class XYPad : public QWidget {
    Q_OBJECT
public:
    explicit XYPad(QWidget *parent = nullptr);

    void setPosition(float x, float y);
    bool isDragging() const { return m_dragging; }

signals:
    void positionChanged(float x, float y); // -1.0 to 1.0

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updatePosition(const QPointF &mousePos);
    float radius() const;
    QPointF center() const;

    QPointF m_stickOffset{0, 0};
    bool m_dragging = false;
};

#endif // XYPAD_H
