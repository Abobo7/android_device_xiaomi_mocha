/*
 * mediatest - temporary diagnostic tool for mocha H.264 ByteBuffer decode.
 * Native C++ MediaCodec (libstagefright) sync mode, BYTEBUFFER output
 * (surface == NULL) - the same ACodec/OMX path UU remote uses.
 *
 * Usage: mediatest <file> [--color-format N] [--iterations N]
 *
 * NOTE: diagnostic tool only. Do not ship in a release image.
 */
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <media/stagefright/MediaErrors.h>
#include <media/MediaCodecBuffer.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/ICrypto.h>
#include <gui/Surface.h>
#include <utils/Log.h>
#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "mediatest", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "mediatest", __VA_ARGS__)

using namespace android;

int main(int argc, char** argv) {
    if (argc < 2) {
        LOGI("usage: mediatest <file> [--color-format N] [--iterations N]");
        return 2;
    }
    const char* path = argv[1];
    int forcedColorFormat = -1;
    int maxRounds = 20000;
    const char* componentName = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--color-format") && i + 1 < argc)
            forcedColorFormat = (int)strtol(argv[i + 1], NULL, 0);
        if (!strcmp(argv[i], "--iterations") && i + 1 < argc)
            maxRounds = (int)strtol(argv[i + 1], NULL, 0);
        if (!strcmp(argv[i], "--component") && i + 1 < argc)
            componentName = argv[i + 1];
    }

    sp<ALooper> looper = new ALooper;
    looper->setName("mediatest_looper");
    looper->start();

    sp<NuMediaExtractor> ex = new NuMediaExtractor;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGE("open %s failed", path);
        return 2;
    }
    off64_t fileSize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    status_t st = ex->setDataSource(fd, 0, fileSize);
    close(fd);
    LOGI("setDataSource(%s) = %d", path, st);
    if (st != OK) return 2;

    /* find first video track */
    int vt = -1;
    sp<AMessage> fmt;
    for (int i = 0; ; i++) {
        sp<AMessage> tfmt;
        if (ex->getTrackFormat(i, &tfmt) != OK) break;
        AString mime;
        tfmt->findString("mime", &mime);
        int32_t w = 0, h = 0;
        tfmt->findInt32("width", &w);
        tfmt->findInt32("height", &h);
        LOGI("track %d mime=%s %dx%d", i, mime.c_str(), w, h);
        if (vt < 0 && !strncmp(mime.c_str(), "video/", 6)) {
            vt = i;
            fmt = tfmt;
        }
    }
    if (vt < 0) {
        LOGE("no video track");
        return 2;
    }
    ex->selectTrack(vt);

    AString mime;
    fmt->findString("mime", &mime);
    if (forcedColorFormat >= 0) {
        fmt->setInt32("color-format", forcedColorFormat);
        LOGI("forcing color-format %#x", forcedColorFormat);
    }

    status_t err;
    sp<MediaCodec> codec;
    if (componentName != NULL) {
        codec = MediaCodec::CreateByComponentName(looper, AString(componentName), &err);
        LOGI("createByComponentName(%s) err=%d", componentName, err);
    } else {
        codec = MediaCodec::CreateByType(looper, mime, false /*encoder*/, &err);
        LOGI("createByType(%s) err=%d", mime.c_str(), err);
    }
    if (codec == NULL) {
        LOGE("create decoder failed err=%d", err);
        return 2;
    }
    LOGI("codec created, configuring (ByteBuffer output)...");

    /* surface == NULL => ByteBuffer output, exactly like UU remote. */
    err = codec->configure(fmt, NULL /*surface*/, NULL /*crypto*/, 0 /*flags*/);
    LOGI("configure = %d", err);
    if (err != OK) {
        LOGE("configure failed");
        return 3;
    }

    err = codec->start();
    LOGI("start = %d", err);

    /* initial output format (color-format before any frame) */
    sp<AMessage> of;
    if (codec->getOutputFormat(&of) == OK) {
        int32_t cf = -1;
        of->findInt32("color-format", &cf);
        LOGI("initial output format color-format=%#x", cf);
    }

    bool inputEOS = false, outputEOS = false;
    int frames = 0, idle = 0;
    for (int round = 0; round < maxRounds && !outputEOS; round++) {
        /* input */
        if (!inputEOS) {
            size_t inIdx;
            err = codec->dequeueInputBuffer(&inIdx, 10000);
            if (err == OK) {
                sp<MediaCodecBuffer> inBuf;
                codec->getInputBuffer(inIdx, &inBuf);
                sp<ABuffer> sample = new ABuffer(inBuf->capacity());
                status_t rst = ex->readSampleData(sample);
                if (rst != OK) {
                    codec->queueInputBuffer(inIdx, 0, 0, 0, MediaCodec::BUFFER_FLAG_EOS);
                    inputEOS = true;
                    LOGI("queued EOS input");
                } else {
                    int64_t pts = 0;
                    ex->getSampleTime(&pts);
                    memcpy(inBuf->base(), sample->base(), sample->size());
                    codec->queueInputBuffer(inIdx, 0, sample->size(), pts, 0);
                    ex->advance();
                }
            } else if (err != -EAGAIN) {
                LOGI("dequeueInputBuffer error %d", err);
            }
        }
        /* output */
        size_t outIdx = 0, outOffset = 0, outSize = 0;
        int64_t pts = 0;
        uint32_t flags = 0;
        err = codec->dequeueOutputBuffer(&outIdx, &outOffset, &outSize, &pts, &flags, 10000);
        if (err == OK) {
            if (flags & MediaCodec::BUFFER_FLAG_CODECCONFIG) {
                codec->releaseOutputBuffer(outIdx);
                continue;
            }
            if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                LOGI("output EOS, total frames=%d idle=%d", frames, idle);
                outputEOS = true;
                codec->releaseOutputBuffer(outIdx);
                break;
            }
            frames++;
            if (frames <= 5 || frames % 50 == 0)
                LOGI("frame %d pts=%lld size=%zu flags=%u", frames,
                        (long long)pts, (size_t)outSize, flags);
            codec->releaseOutputBuffer(outIdx);
        } else if (err == -EAGAIN) {
            idle++;
            if (idle % 100 == 0) LOGI("idle...");
        } else if (err == INFO_FORMAT_CHANGED) {
            sp<AMessage> of2;
            if (codec->getOutputFormat(&of2) == OK) {
                int32_t cf = -1, ow = 0, oh = 0;
                of2->findInt32("color-format", &cf);
                of2->findInt32("width", &ow);
                of2->findInt32("height", &oh);
                LOGI("INFO_FORMAT_CHANGED: %dx%d color-format=%#x", ow, oh, cf);
            } else {
                LOGI("INFO_FORMAT_CHANGED");
            }
            continue;
        } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGI("INFO_OUTPUT_BUFFERS_CHANGED");
            continue;
        } else if (err == INFO_DISCONTINUITY) {
            LOGI("INFO_DISCONTINUITY");
            continue;
        } else {
            LOGE("dequeueOutputBuffer error %d", err);
            sp<AMessage> of2;
            if (codec->getOutputFormat(&of2) == OK) {
                int32_t cf2 = -1;
                of2->findInt32("color-format", &cf2);
                LOGI("format after error: color-format=%#x", cf2);
            }
            break;
        }
        usleep(500);
    }
    LOGI("DONE: frames=%d idle=%d", frames, idle);
    codec->stop();
    codec->release();
    looper->stop();
    return frames > 0 ? 0 : 1;
}
