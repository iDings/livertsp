#pragma once

#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

namespace LiveRTSP {
class FFGlobal {
public:
    static void Init() {
        static std::once_flag once;
        std::call_once(once, []{
                avdevice_register_all();
                avformat_network_init();
            });
    }
};
}
