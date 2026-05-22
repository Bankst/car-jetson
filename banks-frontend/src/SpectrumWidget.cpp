#include "SpectrumWidget.h"
#include "AudioRingConsumer.h"
#include "Fft.h"
#include "Log.h"

#include <QCoreApplication>
#include <QVariant>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QSGGeometry>

#include <algorithm>
#include <cmath>

namespace {
constexpr int   kFftSize    = 1024;
constexpr float kSampleRate = 44100.0f;
constexpr float kFMin       = 50.0f;
constexpr float kFMax       = 16000.0f;
constexpr float kDecay      = 0.78f;   // bar fall coefficient (exponential)
constexpr float kPeakFall   = 0.012f;  // peak fall per frame (linear, fraction of height)
constexpr float kDbFloor    = -60.0f;
constexpr float kDbCeil     = -5.0f;
constexpr float kPeakThick  = 2.0f;    // peak slab thickness in px
}

SpectrumWidget::SpectrumWidget(QQuickItem* parent)
    : QQuickItem(parent),
      m_bands(m_bandCount, 0.0f),
      m_peaks(m_bandCount, 0.0f),
      m_scratchSamples(kFftSize, 0.0f) {
    setFlag(ItemHasContents, true);
}

SpectrumWidget::~SpectrumWidget() {
    if (m_audio && m_ring) m_audio->removeConsumer(m_ring.get());
}

void SpectrumWidget::setBandCount(int n) {
    n = std::clamp(n, 4, 256);
    if (n == m_bandCount) return;
    m_bandCount = n;
    m_bands.assign(n, 0.0f);
    m_peaks.assign(n, 0.0f);
    emit bandCountChanged();
    update();
}

void SpectrumWidget::setBarColor(const QColor& c) {
    if (c == m_barColor) return;
    m_barColor = c;
    emit barColorChanged();
    update();
}

void SpectrumWidget::itemChange(ItemChange change, const ItemChangeData& data) {
    if (change == ItemSceneChange) {
        if (data.window) {
            m_audio = reinterpret_cast<AudioCapture*>(
                qApp->property("audioCapturePtr").value<quintptr>());
            if (m_audio) {
                m_ring = std::make_unique<AudioRingConsumer>(8192);
                m_fft  = std::make_unique<Fft>(kFftSize);
                m_audio->addConsumer(m_ring.get());
                qCInfo(logSpectrum) << "SpectrumWidget registered as audio consumer";
            } else {
                qCWarning(logSpectrum) << "no AudioCapture singleton attached to qApp";
            }
            if (auto* w = data.window) {
                connect(w, &QQuickWindow::beforeRendering, this, &SpectrumWidget::onTick,
                        Qt::DirectConnection);
            }
        } else {
            if (m_audio && m_ring) m_audio->removeConsumer(m_ring.get());
            m_ring.reset();
            m_fft.reset();
            m_audio = nullptr;
        }
    }
    QQuickItem::itemChange(change, data);
}

void SpectrumWidget::onTick() {
    QMetaObject::invokeMethod(this, [this]{ update(); }, Qt::QueuedConnection);
}

QSGNode* SpectrumWidget::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    if (!m_firstPaintLogged) {
        qCInfo(logSpectrum) << "first paint, size=" << width() << "x" << height()
                            << "bands=" << m_bandCount;
        m_firstPaintLogged = true;
    }

    // --- FFT + smoothing ---
    if (m_ring && m_fft) {
        m_ring->peekLatest(m_scratchSamples.data(), kFftSize);
        const float* mag = m_fft->magnitudes(m_scratchSamples.data());
        std::vector<float> bandsRaw(m_bandCount, 0.0f);
        m_fft->logBands(mag, bandsRaw.data(), m_bandCount, kFMin, kFMax, kSampleRate);
        for (int i = 0; i < m_bandCount; ++i) {
            float v = bandsRaw[i];
            float db = (v > 1e-7f) ? 20.0f * std::log10(v) : kDbFloor;
            float norm = std::clamp((db - kDbFloor) / (kDbCeil - kDbFloor), 0.0f, 1.0f);
            // bar exponential decay
            m_bands[i] = std::max(norm, m_bands[i] * kDecay);
            // peak: snap up, linear fall
            if (m_bands[i] > m_peaks[i]) m_peaks[i] = m_bands[i];
            else                         m_peaks[i] = std::max(0.0f, m_peaks[i] - kPeakFall);
        }
    }

    // --- Geometry: 6 verts/bar + 6 verts/peak = 12/band, colored ---
    const int nBars  = m_bandCount;
    const int nVerts = nBars * 12;

    auto* node = static_cast<QSGGeometryNode*>(old);
    QSGGeometry* geo = nullptr;

    if (!node) {
        node = new QSGGeometryNode();
        geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), nVerts);
        geo->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geo);
        node->setFlag(QSGNode::OwnsGeometry);

        auto* mat = new QSGVertexColorMaterial();
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
    } else {
        geo = node->geometry();
        if (geo->vertexCount() != nVerts) geo->allocate(nVerts);
    }

    const float W = float(width());
    const float H = float(height());
    if (W > 0 && H > 0 && nBars > 0) {
        const float gap   = std::max(1.0f, W / float(nBars) * 0.15f);
        const float bw    = std::max(1.0f, W / float(nBars) - gap);

        QSGGeometry::ColoredPoint2D* v = geo->vertexDataAsColoredPoint2D();

        auto setV = [](QSGGeometry::ColoredPoint2D& p, float x, float y,
                       uchar r, uchar g, uchar b, uchar a) {
            p.x = x; p.y = y; p.r = r; p.g = g; p.b = b; p.a = a;
        };

        for (int i = 0; i < nBars; ++i) {
            // Band hue: low freq = warm (red/orange), high = cool (cyan/violet)
            const float t = (nBars > 1) ? float(i) / float(nBars - 1) : 0.0f;
            // Hue ramp ~ red(0°) -> yellow(60°) -> green(120°) -> cyan(180°) -> blue(240°)
            const float hue = 0.0f + t * 240.0f;
            QColor base = QColor::fromHsvF(hue / 360.0f, 0.85f, 1.0f);
            uchar br = base.red(), bg = base.green(), bb = base.blue();

            float x0 = float(i) * (bw + gap);
            float x1 = x0 + bw;
            float bh = m_bands[i] * H;
            float ybarTop = H - bh;
            float ybarBot = H;

            // Bar — top dim, bottom bright (vertical gradient via per-vertex color)
            uchar topA = 0xff, botA = 0xff;
            uchar topR = uchar(br * 0.55f), topG = uchar(bg * 0.55f), topB_ = uchar(bb * 0.55f);

            // tri 1 (TL, TR, BL)
            setV(v[i*12 + 0], x0, ybarTop, topR, topG, topB_, topA);
            setV(v[i*12 + 1], x1, ybarTop, topR, topG, topB_, topA);
            setV(v[i*12 + 2], x0, ybarBot, br, bg, bb, botA);
            // tri 2 (TR, BR, BL)
            setV(v[i*12 + 3], x1, ybarTop, topR, topG, topB_, topA);
            setV(v[i*12 + 4], x1, ybarBot, br, bg, bb, botA);
            setV(v[i*12 + 5], x0, ybarBot, br, bg, bb, botA);

            // Peak slab — small floating rectangle at peak height
            float pkY  = H - m_peaks[i] * H;
            float pkY0 = pkY - kPeakThick * 0.5f;
            float pkY1 = pkY + kPeakThick * 0.5f;
            if (pkY1 > H) { pkY1 = H; pkY0 = H - kPeakThick; }
            if (pkY0 < 0) { pkY0 = 0; pkY1 = kPeakThick; }

            // Peak color: bright white-tinted band color
            uchar pr = uchar(std::min(255, int(br) + 80));
            uchar pg = uchar(std::min(255, int(bg) + 80));
            uchar pb = uchar(std::min(255, int(bb) + 80));

            // Hide peak if at floor and bar empty
            uchar pa = (m_peaks[i] < 0.02f) ? 0 : 0xff;

            setV(v[i*12 + 6], x0, pkY0, pr, pg, pb, pa);
            setV(v[i*12 + 7], x1, pkY0, pr, pg, pb, pa);
            setV(v[i*12 + 8], x0, pkY1, pr, pg, pb, pa);
            setV(v[i*12 + 9], x1, pkY0, pr, pg, pb, pa);
            setV(v[i*12 + 10], x1, pkY1, pr, pg, pb, pa);
            setV(v[i*12 + 11], x0, pkY1, pr, pg, pb, pa);
        }
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
