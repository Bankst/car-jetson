#pragma once

#include <QQuickItem>
#include <QColor>
#include <memory>
#include <vector>

#include "AudioCapture.h"

class AudioRingConsumer;
class Fft;

class SpectrumWidget : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int    bandCount READ bandCount WRITE setBandCount NOTIFY bandCountChanged)
    Q_PROPERTY(QColor barColor  READ barColor  WRITE setBarColor  NOTIFY barColorChanged)
    Q_PROPERTY(int    fftSize   READ fftSize   CONSTANT)

public:
    explicit SpectrumWidget(QQuickItem* parent = nullptr);
    ~SpectrumWidget() override;

    int bandCount() const { return m_bandCount; }
    void setBandCount(int n);

    QColor barColor() const { return m_barColor; }
    void setBarColor(const QColor& c);

    int fftSize() const { return 1024; }

signals:
    void bandCountChanged();
    void barColorChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    void onTick();

    int m_bandCount = 32;
    QColor m_barColor{0xcc, 0xff, 0xff, 0xff};

    AudioCapture* m_audio = nullptr;
    std::unique_ptr<AudioRingConsumer> m_ring;
    std::unique_ptr<Fft> m_fft;
    std::vector<float>   m_bands;   // smoothed values 0..1
    std::vector<float>   m_peaks;   // peak-hold values 0..1
    std::vector<float>   m_scratchSamples;
    bool                 m_firstPaintLogged = false;
};
