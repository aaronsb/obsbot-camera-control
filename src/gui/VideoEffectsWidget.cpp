#include "VideoEffectsWidget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtGlobal>
#include <cmath>

namespace {

int sliderPositionForRange(float value, float min, float max)
{
    if (qFuzzyCompare(max, min)) {
        return 50;
    }
    const float clamped = qBound(min, value, max);
    return static_cast<int>(std::round(((clamped - min) / (max - min)) * 100.0f));
}

float valueFromSlider(int position, float min, float max)
{
    const float t = qBound(0.0f, position / 100.0f, 1.0f);
    return min + (max - min) * t;
}

QWidget *createLabeledSlider(const QString &label, QSlider *slider)
{
    QWidget *container = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QLabel *title = new QLabel(label, container);
    title->setMinimumWidth(110);
    layout->addWidget(title);
    layout->addWidget(slider, 1);
    return container;
}

} // namespace

VideoEffectsWidget::VideoEffectsWidget(QWidget *parent)
    : QWidget(parent)
    , m_horizontalFlipCheckBox(nullptr)
    , m_shadowColorButton(nullptr)
    , m_highlightColorButton(nullptr)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);

    QLabel *infoLabel = new QLabel(tr("Creative adjustments are applied in software and do not change the camera's onboard settings."), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
    rootLayout->addWidget(infoLabel);

    auto addSlider = [&](QVBoxLayout *groupLayout, const QString &label, float min, float max, float initial, std::function<void(float)> setter) {
        QSlider *slider = createSlider(this);
        slider->setValue(sliderPositionForRange(initial, min, max));
        bindSlider(slider, min, max, std::move(setter), initial);
        groupLayout->addWidget(createLabeledSlider(label, slider));
    };

    // Tone adjustments
    QGroupBox *toneGroup = new QGroupBox(tr("Tone"));
    QVBoxLayout *toneLayout = new QVBoxLayout(toneGroup);
    toneLayout->setSpacing(6);
    addSlider(toneLayout, tr("Brightness"), -0.5f, 0.5f, m_settings.brightness, [this](float v) {
        m_settings.brightness = v;
        emitSettingsChanged();
    });
    addSlider(toneLayout, tr("Contrast"), -0.5f, 0.5f, m_settings.contrast, [this](float v) {
        m_settings.contrast = v;
        emitSettingsChanged();
    });
    addSlider(toneLayout, tr("Exposure"), -2.0f, 2.0f, m_settings.exposure, [this](float v) {
        m_settings.exposure = v;
        emitSettingsChanged();
    });
    addSlider(toneLayout, tr("Highlights"), -0.5f, 0.5f, m_settings.highlights, [this](float v) {
        m_settings.highlights = v;
        emitSettingsChanged();
    });
    addSlider(toneLayout, tr("Shadows"), -0.5f, 0.5f, m_settings.shadows, [this](float v) {
        m_settings.shadows = v;
        emitSettingsChanged();
    });
    rootLayout->addWidget(toneGroup);

    // Color adjustments
    QGroupBox *colorGroup = new QGroupBox(tr("Color"));
    QVBoxLayout *colorLayout = new QVBoxLayout(colorGroup);
    colorLayout->setSpacing(6);
    addSlider(colorLayout, tr("Saturation"), -1.0f, 1.0f, m_settings.saturation, [this](float v) {
        m_settings.saturation = v;
        emitSettingsChanged();
    });
    addSlider(colorLayout, tr("Vibrance"), -1.0f, 1.0f, m_settings.vibrance, [this](float v) {
        m_settings.vibrance = v;
        emitSettingsChanged();
    });
    addSlider(colorLayout, tr("Temperature"), -0.2f, 0.2f, m_settings.temperature, [this](float v) {
        m_settings.temperature = v;
        emitSettingsChanged();
    });
    addSlider(colorLayout, tr("Tint"), -0.2f, 0.2f, m_settings.tint, [this](float v) {
        m_settings.tint = v;
        emitSettingsChanged();
    });

    // Duo tone controls
    QWidget *duoToneRow = new QWidget(colorGroup);
    QHBoxLayout *duoToneLayout = new QHBoxLayout(duoToneRow);
    duoToneLayout->setContentsMargins(0, 0, 0, 0);
    duoToneLayout->setSpacing(8);

    QLabel *duoToneLabel = new QLabel(tr("Duo Tone"), duoToneRow);
    duoToneLayout->addWidget(duoToneLabel);

    m_shadowColorButton = new QPushButton(tr("Shadow Color"), duoToneRow);
    updateColorButton(m_shadowColorButton, m_settings.duoToneShadow);
    connect(m_shadowColorButton, &QPushButton::clicked, this, [this]() {
        QColor chosen = QColorDialog::getColor(m_settings.duoToneShadow, this, tr("Select Shadow Color"));
        if (chosen.isValid()) {
            m_settings.duoToneShadow = chosen;
            updateColorButton(m_shadowColorButton, chosen);
            emitSettingsChanged();
        }
    });
    duoToneLayout->addWidget(m_shadowColorButton);

    m_highlightColorButton = new QPushButton(tr("Highlight Color"), duoToneRow);
    updateColorButton(m_highlightColorButton, m_settings.duoToneHighlight);
    connect(m_highlightColorButton, &QPushButton::clicked, this, [this]() {
        QColor chosen = QColorDialog::getColor(m_settings.duoToneHighlight, this, tr("Select Highlight Color"));
        if (chosen.isValid()) {
            m_settings.duoToneHighlight = chosen;
            updateColorButton(m_highlightColorButton, chosen);
            emitSettingsChanged();
        }
    });
    duoToneLayout->addWidget(m_highlightColorButton);
    duoToneLayout->addStretch(1);
    colorLayout->addWidget(duoToneRow);

    QSlider *duoToneSlider = createSlider(this);
    duoToneSlider->setValue(sliderPositionForRange(m_settings.duoToneIntensity, 0.0f, 1.0f));
    bindSlider(duoToneSlider, 0.0f, 1.0f, [this](float v) {
        m_settings.duoToneIntensity = v;
        emitSettingsChanged();
    }, m_settings.duoToneIntensity);
    colorLayout->addWidget(createLabeledSlider(tr("Duo Tone Mix"), duoToneSlider));

    rootLayout->addWidget(colorGroup);

    // Effects adjustments
    QGroupBox *effectsGroup = new QGroupBox(tr("Creative Effects"));
    QVBoxLayout *effectsLayout = new QVBoxLayout(effectsGroup);
    effectsLayout->setSpacing(6);
    addSlider(effectsLayout, tr("Noise"), 0.0f, 0.4f, m_settings.noise, [this](float v) {
        m_settings.noise = v;
        emitSettingsChanged();
    });
    addSlider(effectsLayout, tr("Blur"), 0.0f, 1.0f, m_settings.blur, [this](float v) {
        m_settings.blur = v;
        emitSettingsChanged();
    });
    addSlider(effectsLayout, tr("Sharpen"), 0.0f, 1.0f, m_settings.sharpen, [this](float v) {
        m_settings.sharpen = v;
        emitSettingsChanged();
    });
    addSlider(effectsLayout, tr("Glow"), 0.0f, 1.0f, m_settings.glow, [this](float v) {
        m_settings.glow = v;
        emitSettingsChanged();
    });
    addSlider(effectsLayout, tr("Bloom"), 0.0f, 1.0f, m_settings.bloom, [this](float v) {
        m_settings.bloom = v;
        emitSettingsChanged();
    });
    addSlider(effectsLayout, tr("Soft Focus"), 0.0f, 1.0f, m_settings.softFocus, [this](float v) {
        m_settings.softFocus = v;
        emitSettingsChanged();
    });
    rootLayout->addWidget(effectsGroup);

    // Orientation
    QGroupBox *orientationGroup = new QGroupBox(tr("Orientation"));
    QVBoxLayout *orientationLayout = new QVBoxLayout(orientationGroup);
    orientationLayout->setSpacing(6);

    m_horizontalFlipCheckBox = new QCheckBox(tr("Mirror (horizontal flip)"), orientationGroup);
    m_horizontalFlipCheckBox->setChecked(m_settings.horizontalFlip);
    connect(m_horizontalFlipCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.horizontalFlip = checked;
        emitSettingsChanged();
    });
    orientationLayout->addWidget(m_horizontalFlipCheckBox);
    rootLayout->addWidget(orientationGroup);

    // Reset button
    QPushButton *resetButton = new QPushButton(tr("Reset Adjustments"), this);
    resetButton->setObjectName("secondaryAction");
    connect(resetButton, &QPushButton::clicked, this, &VideoEffectsWidget::reset);
    rootLayout->addWidget(resetButton);

    rootLayout->addStretch(1);
}

void VideoEffectsWidget::applySettings(const FilterPreviewWidget::VideoEffectsSettings &settings)
{
    m_settings = settings;

    // Update UI controls to reflect new settings
    const auto syncSlider = [&](QGroupBox *group, const QString &label, float min, float max, float value) {
        const QList<QSlider *> sliders = group->findChildren<QSlider *>();
        for (QSlider *slider : sliders) {
            QLabel *associatedLabel = slider->parentWidget()->findChild<QLabel *>();
            if (associatedLabel && associatedLabel->text() == label) {
                slider->blockSignals(true);
                slider->setValue(sliderPositionForRange(value, min, max));
                slider->blockSignals(false);
                break;
            }
        }
    };

    const QList<QGroupBox *> groups = this->findChildren<QGroupBox *>();
    for (QGroupBox *group : groups) {
        if (group->title() == tr("Tone")) {
            syncSlider(group, tr("Brightness"), -0.5f, 0.5f, m_settings.brightness);
            syncSlider(group, tr("Contrast"), -0.5f, 0.5f, m_settings.contrast);
            syncSlider(group, tr("Exposure"), -2.0f, 2.0f, m_settings.exposure);
            syncSlider(group, tr("Highlights"), -0.5f, 0.5f, m_settings.highlights);
            syncSlider(group, tr("Shadows"), -0.5f, 0.5f, m_settings.shadows);
        } else if (group->title() == tr("Color")) {
            syncSlider(group, tr("Saturation"), -1.0f, 1.0f, m_settings.saturation);
            syncSlider(group, tr("Vibrance"), -1.0f, 1.0f, m_settings.vibrance);
            syncSlider(group, tr("Temperature"), -0.2f, 0.2f, m_settings.temperature);
            syncSlider(group, tr("Tint"), -0.2f, 0.2f, m_settings.tint);
            syncSlider(group, tr("Duo Tone Mix"), 0.0f, 1.0f, m_settings.duoToneIntensity);
        } else if (group->title() == tr("Creative Effects")) {
            syncSlider(group, tr("Noise"), 0.0f, 0.4f, m_settings.noise);
            syncSlider(group, tr("Blur"), 0.0f, 1.0f, m_settings.blur);
            syncSlider(group, tr("Sharpen"), 0.0f, 1.0f, m_settings.sharpen);
            syncSlider(group, tr("Glow"), 0.0f, 1.0f, m_settings.glow);
            syncSlider(group, tr("Bloom"), 0.0f, 1.0f, m_settings.bloom);
            syncSlider(group, tr("Soft Focus"), 0.0f, 1.0f, m_settings.softFocus);
        }
    }

    if (m_horizontalFlipCheckBox) {
        m_horizontalFlipCheckBox->blockSignals(true);
        m_horizontalFlipCheckBox->setChecked(m_settings.horizontalFlip);
        m_horizontalFlipCheckBox->blockSignals(false);
    }
    if (m_shadowColorButton) {
        updateColorButton(m_shadowColorButton, m_settings.duoToneShadow);
    }
    if (m_highlightColorButton) {
        updateColorButton(m_highlightColorButton, m_settings.duoToneHighlight);
    }

    emitSettingsChanged();
}

void VideoEffectsWidget::reset()
{
    applySettings(FilterPreviewWidget::VideoEffectsSettings::defaults());
}

QSlider *VideoEffectsWidget::createSlider(QWidget *parent) const
{
    QSlider *slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, 100);
    slider->setSingleStep(1);
    slider->setPageStep(5);
    return slider;
}

void VideoEffectsWidget::bindSlider(QSlider *slider, float min, float max, std::function<void(float)> setter, float initial)
{
    slider->setValue(sliderPositionForRange(initial, min, max));
    connect(slider, &QSlider::valueChanged, this, [this, min, max, setter = std::move(setter)](int value) {
        setter(valueFromSlider(value, min, max));
    });
}

void VideoEffectsWidget::updateColorButton(QPushButton *button, const QColor &color)
{
    const QString css = QStringLiteral("background-color: %1; color: %2;").arg(color.name(), color.lightness() > 128 ? "#111" : "#f0f0f0");
    button->setStyleSheet(css);
}

void VideoEffectsWidget::emitSettingsChanged()
{
    emit effectsChanged(m_settings);
}
