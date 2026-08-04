#ifndef VIDEOEFFECTSWIDGET_H
#define VIDEOEFFECTSWIDGET_H

#include "FilterPreviewWidget.h"

#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QWidget;

class VideoEffectsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoEffectsWidget(QWidget *parent = nullptr);

    FilterPreviewWidget::VideoEffectsSettings settings() const { return m_settings; }

signals:
    void effectsChanged(const FilterPreviewWidget::VideoEffectsSettings &settings);
    void paperCropIntentEdited(const PaperCropSettings &settings);
    void paperDetectionResetRequested();

public slots:
    void applySettings(const FilterPreviewWidget::VideoEffectsSettings &settings);
    void applyPaperCropForScene(const PaperCropSettings &settings);
    void reset();
    void setPaperDetected(bool detected);

private:
    QSlider *createSlider(QWidget *parent) const;
    void bindSlider(QSlider *slider, float min, float max, std::function<void(float)> setter, float initial);
    void updateColorButton(QPushButton *button, const QColor &color);
    void emitSettingsChanged();
    void updateCropControls();

    FilterPreviewWidget::VideoEffectsSettings m_settings;
    QCheckBox *m_horizontalFlipCheckBox;
    QComboBox *m_cropModeCombo;
    QWidget *m_cropMarginsContainer;
    QLabel *m_cropStatusLabel;
    bool m_paperDetected;
    QSlider *m_cropLeftSlider;
    QSlider *m_cropTopSlider;
    QSlider *m_cropRightSlider;
    QSlider *m_cropBottomSlider;
    QPushButton *m_shadowColorButton;
    QPushButton *m_highlightColorButton;
};

#endif // VIDEOEFFECTSWIDGET_H
