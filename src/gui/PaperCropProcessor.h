#ifndef PAPERCROPPROCESSOR_H
#define PAPERCROPPROCESSOR_H

#include "PaperCropSettings.h"

#include <QImage>
#include <memory>

class PaperCropProcessor
{
public:
    PaperCropProcessor();
    ~PaperCropProcessor();

    PaperCropProcessor(const PaperCropProcessor &) = delete;
    PaperCropProcessor &operator=(const PaperCropProcessor &) = delete;

    QImage process(const QImage &image, const PaperCropSettings &settings);
    bool paperDetected() const;

    static bool automaticDetectionAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // PAPERCROPPROCESSOR_H
