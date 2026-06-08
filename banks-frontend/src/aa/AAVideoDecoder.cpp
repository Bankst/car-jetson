extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifndef BANKS_AA_NVDEC
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#endif
}

#include "AAVideoDecoder.h"
#include <QDebug>

#ifdef BANKS_AA_NVDEC
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <v4l2_nv_extensions.h>
#include <nvbufsurface.h>
#include <nvbufsurftransform.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#endif

AAVideoDecoder::AAVideoDecoder() = default;

AAVideoDecoder::~AAVideoDecoder() {
    close();
}

// ─── NVDEC path ──────────────────────────────────────────────────────────────
#ifdef BANKS_AA_NVDEC

bool AAVideoDecoder::openNvdec() {
    m_v4l2Fd = v4l2_open("/dev/nvhost-nvdec", O_RDWR | O_NONBLOCK);
    if (m_v4l2Fd < 0) {
        qWarning("[NVDEC] failed to open /dev/nvhost-nvdec: %s", strerror(errno));
        return false;
    }

    // Verify M2M capability
    v4l2_capability caps{};
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_QUERYCAP, &caps) < 0 ||
        !(caps.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
        qWarning("[NVDEC] device lacks V4L2_CAP_VIDEO_M2M_MPLANE");
        v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
        return false;
    }

    // Output plane = compressed H264 input
    v4l2_format ofmt{};
    ofmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ofmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    ofmt.fmt.pix_mp.num_planes = 1;
    ofmt.fmt.pix_mp.plane_fmt[0].sizeimage = kOutputBufSize;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_S_FMT, &ofmt) < 0) {
        qWarning("[NVDEC] VIDIOC_S_FMT output failed: %s", strerror(errno));
        v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
        return false;
    }

    // Subscribe to resolution change event
    v4l2_event_subscription sub{};
    sub.type = V4L2_EVENT_RESOLUTION_CHANGE;
    v4l2_ioctl(m_v4l2Fd, VIDIOC_SUBSCRIBE_EVENT, &sub);

    // Request output buffers
    v4l2_requestbuffers reqOut{};
    reqOut.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqOut.memory = V4L2_MEMORY_MMAP;
    reqOut.count = kOutputBufCount;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_REQBUFS, &reqOut) < 0) {
        qWarning("[NVDEC] REQBUFS output failed: %s", strerror(errno));
        v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
        return false;
    }

    // MMAP output buffers
    for (int i = 0; i < kOutputBufCount; i++) {
        v4l2_buffer buf{};
        v4l2_plane plane{};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = &plane;
        buf.length = 1;
        if (v4l2_ioctl(m_v4l2Fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qWarning("[NVDEC] QUERYBUF output %d failed", i);
            v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
            return false;
        }
        v4l2_exportbuffer expbuf{};
        expbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        expbuf.index = i;
        expbuf.plane = 0;
        if (v4l2_ioctl(m_v4l2Fd, VIDIOC_EXPBUF, &expbuf) < 0) {
            qWarning("[NVDEC] EXPBUF output %d failed: %s", i, strerror(errno));
            v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
            return false;
        }
        m_outputPlanes[i].fd = expbuf.fd;
        m_outputPlanes[i].length = plane.length;
        m_outputPlanes[i].ptr = mmap(nullptr, plane.length,
            PROT_READ | PROT_WRITE, MAP_SHARED, expbuf.fd, plane.m.mem_offset);
        if (m_outputPlanes[i].ptr == MAP_FAILED) {
            qWarning("[NVDEC] mmap output %d failed", i);
            m_outputPlanes[i].ptr = nullptr;
            ::close(expbuf.fd);
            m_outputPlanes[i].fd = -1;
            v4l2_close(m_v4l2Fd); m_v4l2Fd = -1;
            return false;
        }
    }

    // Stream on output plane
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning("[NVDEC] STREAMON output failed: %s", strerror(errno));
        closeNvdec();
        return false;
    }

    qInfo("[NVDEC] opened /dev/nvhost-nvdec, %d output buffers", kOutputBufCount);
    return true;
}

bool AAVideoDecoder::handleResolutionChange() {
    // Query actual decoded format
    v4l2_format cfmt{};
    cfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_G_FMT, &cfmt) < 0) {
        qWarning("[NVDEC] VIDIOC_G_FMT capture failed");
        return false;
    }

    m_captureWidth = cfmt.fmt.pix_mp.width;
    m_captureHeight = cfmt.fmt.pix_mp.height;

    // Set capture plane format: NV12M (multi-plane NV12)
    cfmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_S_FMT, &cfmt) < 0) {
        qWarning("[NVDEC] VIDIOC_S_FMT capture NV12M failed");
        return false;
    }

    // Get minimum buffer count
    v4l2_control ctrl{};
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    v4l2_ioctl(m_v4l2Fd, VIDIOC_G_CTRL, &ctrl);
    int minBufs = ctrl.value > 0 ? ctrl.value + 4 : 8;
    if (minBufs > kMaxCaptureBufs) minBufs = kMaxCaptureBufs;

    // Request capture buffers
    v4l2_requestbuffers reqCap{};
    reqCap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqCap.memory = V4L2_MEMORY_MMAP;
    reqCap.count = minBufs;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_REQBUFS, &reqCap) < 0) {
        qWarning("[NVDEC] REQBUFS capture failed");
        return false;
    }
    m_captureBufCount = reqCap.count;

    // MMAP and queue capture buffers
    for (int i = 0; i < m_captureBufCount; i++) {
        v4l2_buffer buf{};
        v4l2_plane planes[2]{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 2; // NV12M = 2 planes
        if (v4l2_ioctl(m_v4l2Fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qWarning("[NVDEC] QUERYBUF capture %d failed", i);
            return false;
        }
        for (int p = 0; p < 2; p++) {
            v4l2_exportbuffer expbuf{};
            expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            expbuf.index = i;
            expbuf.plane = p;
            if (v4l2_ioctl(m_v4l2Fd, VIDIOC_EXPBUF, &expbuf) < 0) {
                qWarning("[NVDEC] EXPBUF capture %d plane %d failed: %s", i, p, strerror(errno));
                return false;
            }
            m_capturePlanes[i][p].fd = expbuf.fd;
            m_capturePlanes[i][p].length = planes[p].length;
            m_capturePlanes[i][p].ptr = mmap(nullptr, planes[p].length,
                PROT_READ | PROT_WRITE, MAP_SHARED, expbuf.fd, planes[p].m.mem_offset);
            if (m_capturePlanes[i][p].ptr == MAP_FAILED) {
                qWarning("[NVDEC] mmap capture %d plane %d failed", i, p);
                m_capturePlanes[i][p].ptr = nullptr;
                ::close(expbuf.fd);
                m_capturePlanes[i][p].fd = -1;
                return false;
            }
        }
        // Queue the buffer
        if (v4l2_ioctl(m_v4l2Fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning("[NVDEC] QBUF capture %d failed", i);
            return false;
        }
    }

    // Stream on capture plane
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (v4l2_ioctl(m_v4l2Fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning("[NVDEC] STREAMON capture failed: %s", strerror(errno));
        return false;
    }

    m_captureReady = true;
    qInfo("[NVDEC] capture ready: %dx%d NV12M, %d buffers",
          m_captureWidth, m_captureHeight, m_captureBufCount);
    return true;
}

void AAVideoDecoder::processDecodedFrame(int bufIndex) {
    int w = m_captureWidth;
    int h = m_captureHeight;
    int outH = m_height > 0 ? m_height : h;

    // Get source surface (block-linear NV12 from decoder)
    int dmaBufFd = m_capturePlanes[bufIndex][0].fd;
    void* srcPtr = nullptr;
    if (NvBufSurfaceFromFd(dmaBufFd, &srcPtr) < 0 || !srcPtr) {
        static int errCnt = 0;
        if (++errCnt <= 3) fprintf(stderr, "[NVDEC] NvBufSurfaceFromFd failed fd=%d\n", dmaBufFd);
        return;
    }
    auto* srcSurf = static_cast<NvBufSurface*>(srcPtr);

    // Lazy-create pitch-linear destination surface for detiling
    if (!m_dstSurf) {
        NvBufSurfaceCreateParams params{};
        params.width = w;
        params.height = h;
        params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        params.layout = NVBUF_LAYOUT_PITCH;
        params.memType = NVBUF_MEM_SURFACE_ARRAY;
        NvBufSurface* dst = nullptr;
        if (NvBufSurfaceCreate(&dst, 1, &params) < 0 || !dst) {
            fprintf(stderr, "[NVDEC] NvBufSurfaceCreate pitch-linear failed\n");
            return;
        }
        m_dstSurf = dst;
        fprintf(stderr, "[NVDEC] created pitch-linear dest: %dx%d pitch[0]=%d pitch[1]=%d\n",
                dst->surfaceList[0].width, dst->surfaceList[0].height,
                dst->surfaceList[0].planeParams.pitch[0],
                dst->surfaceList[0].planeParams.pitch[1]);
    }
    auto* dstSurf = static_cast<NvBufSurface*>(m_dstSurf);

    // Transform block-linear → pitch-linear (VIC hardware)
    NvBufSurfTransformParams transformParams{};
    transformParams.transform_flag = NVBUFSURF_TRANSFORM_FILTER;
    transformParams.transform_filter = NvBufSurfTransformInter_Nearest;
    if (NvBufSurfTransform(srcSurf, dstSurf, &transformParams) != NvBufSurfTransformError_Success) {
        static int errCnt = 0;
        if (++errCnt <= 3) fprintf(stderr, "[NVDEC] NvBufSurfTransform failed\n");
        return;
    }

    // Map pitch-linear result for CPU read
    if (NvBufSurfaceMap(dstSurf, 0, -1, NVBUF_MAP_READ) < 0) {
        static int errCnt = 0;
        if (++errCnt <= 3) fprintf(stderr, "[NVDEC] NvBufSurfaceMap dst failed\n");
        return;
    }
    NvBufSurfaceSyncForCpu(dstSurf, 0, -1);

    auto* yPtr = static_cast<uint8_t*>(dstSurf->surfaceList[0].mappedAddr.addr[0]);
    auto* uvPtr = static_cast<uint8_t*>(dstSurf->surfaceList[0].mappedAddr.addr[1]);
    int yStride = dstSurf->surfaceList[0].planeParams.pitch[0];
    int uvStride = dstSurf->surfaceList[0].planeParams.pitch[1];

    uint8_t* srcData[2] = { yPtr, uvPtr };
    int srcLinesize[2] = { yStride, uvStride };

    auto srcFmt = AV_PIX_FMT_NV12;
    if (!m_sws || m_swsSrcFmt != srcFmt || m_swsWidth != w || m_swsHeight != outH) {
        if (m_sws) sws_freeContext(m_sws);
        m_sws = sws_getContext(w, outH, srcFmt, w, outH, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            NvBufSurfaceUnMap(dstSurf, 0, -1);
            fprintf(stderr, "[NVDEC] sws_getContext failed %dx%d\n", w, outH);
            return;
        }
        m_swsSrcFmt = srcFmt;
        m_swsWidth = w;
        m_swsHeight = outH;
        fprintf(stderr, "[NVDEC] sws: NV12 %dx%d yStride=%d uvStride=%d (pitch-linear via VIC)\n",
                w, outH, yStride, uvStride);
    }

    auto f = std::make_shared<Frame>();
    f->width = w;
    f->height = outH;
    f->rgba.resize(w * outH * 4);

    uint8_t* dst[1] = { f->rgba.data() };
    int dstStride[1] = { w * 4 };
    sws_scale(m_sws, srcData, srcLinesize, 0, outH, dst, dstStride);

    NvBufSurfaceUnMap(dstSurf, 0, -1);

    m_latestFrame.store(std::move(f));
    if (m_frameReady) m_frameReady();
}

void AAVideoDecoder::nvdecDecodeLoop() {
    int nextOutBuf = 0;
    int outBufsQueued = 0;
    int nalCount = 0;

    fprintf(stderr, "[NVDEC] decode thread started, fd=%d\n", m_v4l2Fd);

    while (m_running) {
        // Drain ALL NALs to decoder — never drop, P-frames need reference chain
        while (outBufsQueued < kOutputBufCount) {
            std::vector<uint8_t> nal;
            {
                std::unique_lock lk(m_queueMtx);
                if (m_nalQueue.empty()) break;
                nal = std::move(m_nalQueue.front());
                m_nalQueue.erase(m_nalQueue.begin());
            }

            if (nal.size() > static_cast<size_t>(kOutputBufSize)) continue;

            memcpy(m_outputPlanes[nextOutBuf].ptr, nal.data(), nal.size());

            v4l2_buffer obuf{};
            v4l2_plane oplane{};
            obuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            obuf.memory = V4L2_MEMORY_MMAP;
            obuf.index = nextOutBuf;
            obuf.m.planes = &oplane;
            obuf.length = 1;
            oplane.bytesused = nal.size();

            if (v4l2_ioctl(m_v4l2Fd, VIDIOC_QBUF, &obuf) < 0) continue;
            nextOutBuf = (nextOutBuf + 1) % kOutputBufCount;
            outBufsQueued++;
            nalCount++;
        }

        // Poll — short timeout when active
        pollfd pfd{};
        pfd.fd = m_v4l2Fd;
        pfd.events = POLLIN | POLLOUT;
        int ret = poll(&pfd, 1, m_captureReady ? 1 : 50);

        // Handle resolution change event (only before capture is set up)
        if (!m_captureReady) {
            v4l2_event ev{};
            while (v4l2_ioctl(m_v4l2Fd, VIDIOC_DQEVENT, &ev) == 0) {
                if (ev.type == V4L2_EVENT_RESOLUTION_CHANGE) {
                    fprintf(stderr, "[NVDEC] resolution change\n");
                    handleResolutionChange();
                }
            }
        }

        // Dequeue decoded frames — only process the LAST one, re-queue the rest
        if (m_captureReady) {
            int lastBufIdx = -1;
            v4l2_buffer lastBuf{};
            v4l2_plane lastPlanes[2]{};

            v4l2_buffer cbuf{};
            v4l2_plane cplanes[2]{};
            cbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            cbuf.memory = V4L2_MEMORY_MMAP;
            cbuf.m.planes = cplanes;
            cbuf.length = 2;

            while (v4l2_ioctl(m_v4l2Fd, VIDIOC_DQBUF, &cbuf) == 0) {
                // Re-queue previous frame (skip it)
                if (lastBufIdx >= 0)
                    v4l2_ioctl(m_v4l2Fd, VIDIOC_QBUF, &lastBuf);

                lastBufIdx = cbuf.index;
                memcpy(&lastBuf, &cbuf, sizeof(cbuf));
                memcpy(lastPlanes, cplanes, sizeof(cplanes));
                lastBuf.m.planes = lastPlanes;

                memset(&cbuf, 0, sizeof(cbuf));
                memset(cplanes, 0, sizeof(cplanes));
                cbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                cbuf.memory = V4L2_MEMORY_MMAP;
                cbuf.m.planes = cplanes;
                cbuf.length = 2;
            }

            if (lastBufIdx >= 0) {
                processDecodedFrame(lastBufIdx);
                v4l2_ioctl(m_v4l2Fd, VIDIOC_QBUF, &lastBuf);
            }
        }

        // Reclaim output buffers
        {
            v4l2_buffer dbuf{};
            v4l2_plane dplane{};
            dbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            dbuf.memory = V4L2_MEMORY_MMAP;
            dbuf.m.planes = &dplane;
            dbuf.length = 1;
            while (v4l2_ioctl(m_v4l2Fd, VIDIOC_DQBUF, &dbuf) == 0) {
                outBufsQueued--;
                memset(&dbuf, 0, sizeof(dbuf));
                memset(&dplane, 0, sizeof(dplane));
                dbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                dbuf.memory = V4L2_MEMORY_MMAP;
                dbuf.m.planes = &dplane;
                dbuf.length = 1;
            }
        }

        // Wait for new NALs if idle
        if (outBufsQueued == 0 && ret <= 0) {
            std::unique_lock lk(m_queueMtx);
            m_queueCv.wait_for(lk, std::chrono::milliseconds(4),
                [&]{ return !m_nalQueue.empty() || !m_running; });
        }
    }
}

void AAVideoDecoder::closeNvdec() {
    if (m_v4l2Fd < 0) return;

    int otype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    v4l2_ioctl(m_v4l2Fd, VIDIOC_STREAMOFF, &otype);
    if (m_captureReady) {
        int ctype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        v4l2_ioctl(m_v4l2Fd, VIDIOC_STREAMOFF, &ctype);
    }

    for (int i = 0; i < kOutputBufCount; i++) {
        if (m_outputPlanes[i].ptr)
            munmap(m_outputPlanes[i].ptr, m_outputPlanes[i].length);
        if (m_outputPlanes[i].fd >= 0)
            ::close(m_outputPlanes[i].fd);
        m_outputPlanes[i] = {};
    }
    for (int i = 0; i < m_captureBufCount; i++) {
        for (int p = 0; p < 2; p++) {
            if (m_capturePlanes[i][p].ptr)
                munmap(m_capturePlanes[i][p].ptr, m_capturePlanes[i][p].length);
            if (m_capturePlanes[i][p].fd >= 0)
                ::close(m_capturePlanes[i][p].fd);
            m_capturePlanes[i][p] = {};
        }
    }

    if (m_dstSurf) {
        NvBufSurfaceDestroy(static_cast<NvBufSurface*>(m_dstSurf));
        m_dstSurf = nullptr;
    }

    v4l2_close(m_v4l2Fd);
    m_v4l2Fd = -1;
    m_captureBufCount = 0;
    m_captureReady = false;
    qInfo("[NVDEC] closed");
}

bool AAVideoDecoder::open(int width, int height) {
    m_width = width;
    m_height = height;
    m_sws = nullptr;
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;

    if (!openNvdec()) {
        qWarning("[NVDEC] hardware decode unavailable");
        return false;
    }

    m_running = true;
    m_thread = std::thread(&AAVideoDecoder::decodeLoop, this);
    qInfo("[AAVideoDecoder] opened %dx%d NVDEC", width, height);
    return true;
}

void AAVideoDecoder::close() {
    if (m_running) {
        m_running = false;
        m_queueCv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;
    closeNvdec();
}

void AAVideoDecoder::decodeLoop() {
    nvdecDecodeLoop();
}

// ─── ffmpeg / VAAPI / software path ──────────────────────────────────────────
#else

bool AAVideoDecoder::open(int width, int height) {
    m_width = width;
    m_height = height;

    const AVCodec* codec = nullptr;
    bool hwdec = false;

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qWarning("[AAVideoDecoder] h264 decoder not found");
        return false;
    }

    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->width = width;
    m_ctx->height = height;

    if (av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0) == 0) {
        m_ctx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_ctx->hwaccel_flags |= AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH;
        hwdec = true;
        qInfo("[AAVideoDecoder] VAAPI hw device created");
    } else {
        qInfo("[AAVideoDecoder] VAAPI not available, using software decode");
    }

    if (!hwdec)
        m_ctx->thread_count = 2;

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        qWarning("[AAVideoDecoder] failed to open codec");
        if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); m_hwDeviceCtx = nullptr; }
        avcodec_free_context(&m_ctx);
        return false;
    }

    m_sws = nullptr;
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;

    m_running = true;
    m_thread = std::thread(&AAVideoDecoder::decodeLoop, this);
    qInfo("[AAVideoDecoder] opened %dx%d %s%s", width, height, codec->name,
          hwdec ? " (hw accel)" : " (software)");
    return true;
}

void AAVideoDecoder::close() {
    if (m_running) {
        m_running = false;
        m_queueCv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;
    if (m_ctx) { avcodec_free_context(&m_ctx); }
    if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); m_hwDeviceCtx = nullptr; }
}

void AAVideoDecoder::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();

    while (m_running) {
        std::vector<uint8_t> nal;
        {
            std::unique_lock lk(m_queueMtx);
            m_queueCv.wait(lk, [&]{ return !m_nalQueue.empty() || !m_running; });
            if (!m_running) break;
            nal = std::move(m_nalQueue.front());
            m_nalQueue.erase(m_nalQueue.begin());
        }

        pkt->data = nal.data();
        pkt->size = static_cast<int>(nal.size());
        decodePacket(pkt);
        av_packet_unref(pkt);
    }

    decodePacket(nullptr);
    av_packet_free(&pkt);
}

bool AAVideoDecoder::decodePacket(AVPacket* pkt) {
    int ret = avcodec_send_packet(m_ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) return false;

    AVFrame* frame = av_frame_alloc();
    while (avcodec_receive_frame(m_ctx, frame) == 0) {
        convertFrame(frame);
    }
    av_frame_free(&frame);
    return true;
}

#endif // BANKS_AA_NVDEC

// ─── shared ──────────────────────────────────────────────────────────────────

void AAVideoDecoder::feedNalUnit(const uint8_t* data, size_t size) {
    {
        std::lock_guard lk(m_queueMtx);
        m_nalQueue.emplace_back(data, data + size);
    }
    m_queueCv.notify_one();
}

void AAVideoDecoder::setFrameReadyCallback(FrameCallback cb) {
    m_frameReady = std::move(cb);
}

std::shared_ptr<const AAVideoDecoder::Frame> AAVideoDecoder::latestFrame() const {
    return m_latestFrame.load();
}

#ifndef BANKS_AA_NVDEC
void AAVideoDecoder::convertFrame(AVFrame* avf) {
    AVFrame* swFrame = nullptr;

    if (avf->format == AV_PIX_FMT_VAAPI) {
        swFrame = av_frame_alloc();
        if (av_hwframe_transfer_data(swFrame, avf, 0) < 0) {
            qWarning("[AAVideoDecoder] av_hwframe_transfer_data failed");
            av_frame_free(&swFrame);
            return;
        }
        avf = swFrame;
    }

    auto srcFmt = static_cast<AVPixelFormat>(avf->format);

    if (!m_sws || m_swsSrcFmt != avf->format
               || m_swsWidth != avf->width || m_swsHeight != avf->height) {
        if (m_sws) sws_freeContext(m_sws);
        m_sws = sws_getContext(avf->width, avf->height, srcFmt,
                               avf->width, avf->height, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            qWarning("[AAVideoDecoder] sws_getContext failed for fmt %d %dx%d",
                     avf->format, avf->width, avf->height);
            if (swFrame) av_frame_free(&swFrame);
            return;
        }
        m_swsSrcFmt = avf->format;
        m_swsWidth = avf->width;
        m_swsHeight = avf->height;
        qInfo("[AAVideoDecoder] sws_context: fmt=%d (%s) %dx%d",
              avf->format,
              srcFmt == AV_PIX_FMT_NV12 ? "NV12" :
              srcFmt == AV_PIX_FMT_YUV420P ? "YUV420P" : "other",
              avf->width, avf->height);
    }

    auto f = std::make_shared<Frame>();
    f->width = avf->width;
    f->height = avf->height;
    f->rgba.resize(avf->width * avf->height * 4);

    uint8_t* dst[1] = { f->rgba.data() };
    int dstStride[1] = { avf->width * 4 };
    sws_scale(m_sws, avf->data, avf->linesize, 0, avf->height, dst, dstStride);

    if (swFrame) av_frame_free(&swFrame);

    m_latestFrame.store(std::move(f));
    if (m_frameReady) m_frameReady();
}
#endif
