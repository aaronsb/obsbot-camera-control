#ifndef VIDEOEFFECTSWIDGET_H
#define VIDEOEFFECTSWIDGET_H

#include "FilterPreviewWidget.h"

#include <QWidget>
#include <functional>

class QCheckBox;
class QSlider;
class QPushButton;

class VideoEffectsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoEffectsWidget(QWidget *parent = nullptr);

    FilterPreviewWidget::VideoEffectsSettings settings() const { return m_settings; }

signals:
    void effectsChanged(const FilterPreviewWidget::VideoEffectsSettings &settings);

public slots:
    void applySettings(const FilterPreviewWidget::VideoEffectsSettings &settings);
    void reset();

private:
    QSlider *createSlider(QWidget *parent) const;
    void bindSlider(QSlider *slider, float min, float max, std::function<void(float)> setter, float initial);
    void updateColorButton(QPushButton *button, const QColor &color);
    void emitSettingsChanged();

    FilterPreviewWidget::VideoEffectsSettings m_settings;
    QCheckBox *m_horizontalFlipCheckBox;
    QPushButton *m_shadowColorButton;
    QPushButton *m_highlightColorButton;
};

#endif // VIDEOEFFECTSWIDGET_H
