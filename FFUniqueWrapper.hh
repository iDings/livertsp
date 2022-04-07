#pragma once

#include <memory>
#include "easyloggingpp/easylogging++.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

// https://swarminglogic.com/scribble/2015_05_smartwrappers
namespace LiveRTSP {
struct FFDeleter {
    void operator()(AVFormatContext *s) {
        LOG(INFO) << "free avformatcontext";
        avformat_close_input(&s);
    }
    void operator()(AVDictionary *opts) {
        LOG(INFO) << "free avdictionary";
        av_dict_free(&opts);
    }
};
using FFAVFormatContextUnique = std::unique_ptr<AVFormatContext, FFDeleter>;
using FFAVDictionaryUnique = std::unique_ptr<AVDictionary, FFDeleter>;
}
