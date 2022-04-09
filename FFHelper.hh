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

struct FFFrame {
    FFFrame(AVFrame *input_frame, double pts, int width, int height, int format) {
        frame = av_frame_alloc();
        av_frame_move_ref(frame, input_frame);
    }

    ~FFFrame() {
        av_frame_free(&frame);
    }

    double pts;
    int width;
    int height;
    int format;
    AVFrame *frame;
};

struct FFPacket {
};
}
