#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

// Thin C++ wrapper around KissFFT real FFT. Fixed-size, allocates once.
class Fft {
public:
    explicit Fft(size_t n);
    ~Fft();
    Fft(const Fft&) = delete;
    Fft& operator=(const Fft&) = delete;

    size_t size() const { return m_n; }

    // Apply Hann window + real FFT. Returns magnitudes of bins 0..n/2 (size n/2+1).
    // mag[] is internal storage; valid until next call.
    const float* magnitudes(const float* in);

    // Reduce magnitudes into N log-spaced bands across [fmin..fmax] Hz
    // given sampleRate. Sum-of-magnitudes within band.
    void logBands(const float* mag, float* outBands, int nBands,
                  float fmin, float fmax, float sampleRate) const;

private:
    size_t m_n;
    void*  m_cfg = nullptr;   // kiss_fftr_cfg
    std::vector<float> m_window;
    std::vector<float> m_in;        // windowed input
    std::vector<float> m_out;       // 2*(n/2+1) interleaved re/im
    std::vector<float> m_mag;       // n/2+1
};
