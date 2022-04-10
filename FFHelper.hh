#pragma once

#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace LiveRTSP {
class FFHelper {
public:
    static void Init() {
        static std::once_flag once;
        std::call_once(once, []{
                avdevice_register_all();
                avformat_network_init();
            });
    }
};

// https://docs.microsoft.com/en-us/cpp/cpp/move-constructors-and-move-assignment-operators-cpp?view=msvc-170
struct FFFrame {
    FFFrame() :
        frame(av_frame_alloc()),
        pts(AV_NOPTS_VALUE),
        width(0),
        height(0),
        format(AV_PIX_FMT_NONE)
    {
    }

    FFFrame(AVFrame *input_frame, double pts, int width, int height, int format) :
        frame(av_frame_alloc()),
        pts(pts),
        width(width),
        height(height),
        format(format)
    {
        if (input_frame)
            av_frame_move_ref(frame, input_frame);
    }

    ~FFFrame()
    {
        av_frame_free(&frame);
    }

    FFFrame(const FFFrame&) = delete;
    FFFrame& operator=(const FFFrame&) = delete;

    FFFrame(FFFrame &&rhs)
    {
        *this = std::move(rhs);
    }

    FFFrame& operator=(FFFrame&& rhs) noexcept
    {
        if (this != &rhs) {
            pts = rhs.pts;
            width = rhs.width;
            height = rhs.height;
            format = rhs.format;

            av_frame_unref(frame);
            av_frame_move_ref(frame, rhs.frame);
        }

        return *this;
    }

    void unref() {
        av_frame_unref(frame);
    }

    AVFrame *frame;
    double pts;
    int width;
    int height;
    int format;
};

struct FFPacket {
    FFPacket() :
        pkt(av_packet_alloc())
    {}

    FFPacket(AVPacket *ipkt) :
        pkt(av_packet_alloc())
    {
        if (pkt) av_packet_move_ref(pkt, ipkt);
    }

    ~FFPacket()
    {
        av_packet_free(&pkt);
    }

    FFPacket(FFPacket &&rhs)
    {
        *this = std::move(rhs);
    }

    FFPacket& operator=(FFPacket&& rhs) noexcept
    {
        if (this != &rhs) {
            av_packet_unref(pkt);
            av_packet_move_ref(pkt, rhs.pkt);
        }

        return *this;
    }

    void unref()
    {
        av_packet_unref(pkt);
    }

    AVPacket *pkt;
};
}
