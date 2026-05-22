#include "Fft.h"

#include <kiss_fftr.h>

#include <cmath>
#include <algorithm>

Fft::Fft(size_t n)
    : m_n(n),
      m_window(n),
      m_in(n),
      m_out(2 * (n / 2 + 1)),
      m_mag(n / 2 + 1) {
    m_cfg = kiss_fftr_alloc(static_cast<int>(n), 0, nullptr, nullptr);
    // Hann window
    for (size_t i = 0; i < n; ++i) {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * float(i) / float(n - 1)));
    }
}

Fft::~Fft() {
    if (m_cfg) kiss_fftr_free(m_cfg);
}

const float* Fft::magnitudes(const float* in) {
    for (size_t i = 0; i < m_n; ++i) m_in[i] = in[i] * m_window[i];
    kiss_fft_cpx* out = reinterpret_cast<kiss_fft_cpx*>(m_out.data());
    kiss_fftr(static_cast<kiss_fftr_cfg>(m_cfg), m_in.data(), out);

    const size_t nBins = m_n / 2 + 1;
    const float norm = 2.0f / float(m_n);
    for (size_t i = 0; i < nBins; ++i) {
        float re = out[i].r;
        float im = out[i].i;
        m_mag[i] = std::sqrt(re * re + im * im) * norm;
    }
    return m_mag.data();
}

void Fft::logBands(const float* mag, float* outBands, int nBands,
                   float fmin, float fmax, float sampleRate) const {
    const size_t nBins = m_n / 2 + 1;
    const float binHz = sampleRate / float(m_n);
    const float logMin = std::log(fmin);
    const float logMax = std::log(fmax);

    for (int b = 0; b < nBands; ++b) {
        float fLo = std::exp(logMin + (logMax - logMin) * float(b)     / float(nBands));
        float fHi = std::exp(logMin + (logMax - logMin) * float(b + 1) / float(nBands));
        int kLo = std::max(1, int(std::floor(fLo / binHz)));
        int kHi = std::min<int>(nBins - 1, int(std::ceil(fHi / binHz)));
        if (kHi < kLo) kHi = kLo;
        float sum = 0.0f;
        int cnt = 0;
        for (int k = kLo; k <= kHi; ++k) { sum += mag[k]; ++cnt; }
        outBands[b] = cnt > 0 ? sum / float(cnt) : 0.0f;
    }
}
