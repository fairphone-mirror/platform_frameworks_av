/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
Copyright (C) 2019 Nokia Corporation.
This material, including documentation and any related
computer programs, is protected by copyright controlled by
Nokia Corporation. All rights are reserved. Copying,
including reproducing, storing, adapting or translating, any
or all of this material requires the prior written consent of
Nokia Corporation. This material also contains confidential
information which may not be disclosed to others without the
prior written consent of Nokia Corporation.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "codec"
#include <inttypes.h>
#include <utils/Log.h>

#include "SimplePlayer.h"
#include "WaveWriter.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <mediadrm/ICrypto.h>
#include <media/IMediaHTTPService.h>
#include <media/IMediaPlayerService.h>
#include <media/MediaCodecBuffer.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaCodecList.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>
#include <ui/DisplayConfig.h>

static const char *ozoGuidanceTag = "vendor.ozoaudio.guidance.value";
static const char *ozoOrientationTag = "vendor.ozoaudio.orientation.value";

static void usage(const char *me) {
    fprintf(stderr, "usage: %s [-a] use audio\n"
                    "\t\t[-v] use video\n"
                    "\t\t[-p] playback\n"
                    "\t\t[-S] allocate buffers from a surface\n"
                    "\t\t[-R] render output to surface (enables -S)\n"
                    "\t\t[-T] use render timestamps (enables -R)\n"
                    "\t\t[-o] use OZO audio decoder\n"
                    "\t\t[-O file] write decoded audio to specified file\n"
                    "\t\t[-g guidance] set OZO guidance mode\n"
                    "\t\t[-l orientations] set OZO listener orientation, format [frame_ind yaw pitch roll]\n",
                    me);
    exit(1);
}

namespace android {

struct CodecState {
    sp<MediaCodec> mCodec;
    Vector<sp<MediaCodecBuffer> > mInBuffers;
    Vector<sp<MediaCodecBuffer> > mOutBuffers;
    bool mSignalledInputEOS;
    bool mSawOutputEOS;
    int64_t mNumBuffersDecoded;
    int64_t mNumBytesDecoded;
    bool mIsAudio;
};

static void parseOzoOrientations(KeyedVector<size_t, AString> & events, const char *data) {
    if (!data) return;

    char *literal = strndup(data, strlen(data));
    char *value = strtok(literal, ":");
    while (value != NULL) {
        if (strlen(value) != 0) {
            std::string params(value);

            size_t found = params.find_first_of(" ");
            if (found != std::string::npos) {
                size_t frame = (size_t)std::stoul(params.substr(0, found));
                AString event(params.substr(found + 1).c_str());
                events.add(frame, event);
            }
        }
        value = strtok(NULL, ":");
    }
}

}  // namespace android

static int decode(
        const android::sp<android::ALooper> &looper,
        const char *path,
        bool useAudio,
        bool useVideo,
        const android::sp<android::Surface> &surface,
        bool renderSurface,
        bool useTimestamp,
        const char *output,
        bool isOzoAudio,
        const char *ozoGuidance,
        android::KeyedVector<size_t, android::AString> & ozoEvents,
        android::KeyedVector<size_t, android::AString> & ozoHeadsetEvents) {
    using namespace android;

    static int64_t kTimeout = 500ll;

    WaveWriter *waveWriter = nullptr;

    sp<NuMediaExtractor> extractor = new NuMediaExtractor;
    if (extractor->setDataSource(NULL /* httpService */, path) != OK) {
        fprintf(stderr, "unable to instantiate extractor.\n");
        return 1;
    }

    KeyedVector<size_t, CodecState> stateByTrack;

    bool haveAudio = false;
    bool haveVideo = false;
    for (size_t i = 0; i < extractor->countTracks(); ++i) {
        sp<AMessage> format;
        status_t err = extractor->getTrackFormat(i, &format);
        CHECK_EQ(err, (status_t)OK);

        AString mime;
        if (isOzoAudio) {
            format->setString("mime", MEDIA_MIMETYPE_AUDIO_OZOAUDIO);
        }

        if (ozoGuidance) {
            format->setString(ozoGuidanceTag, ozoGuidance);
        }

        ssize_t eventIndex = ozoEvents.indexOfKey(0);
        if (eventIndex >= 0) {
            format->setString(ozoOrientationTag, ozoEvents.valueAt(eventIndex));
        }

        CHECK(format->findString("mime", &mime));

        bool isAudio = !strncasecmp(mime.c_str(), "audio/", 6);
        bool isVideo = !strncasecmp(mime.c_str(), "video/", 6);

        if (useAudio && !haveAudio && isAudio) {
            haveAudio = true;
        } else if (useVideo && !haveVideo && isVideo) {
            haveVideo = true;
        } else {
            continue;
        }

        ALOGV("selecting track %zu", i);

        err = extractor->selectTrack(i);
        CHECK_EQ(err, (status_t)OK);

        CodecState *state =
            &stateByTrack.editValueAt(stateByTrack.add(i, CodecState()));

        state->mNumBytesDecoded = 0;
        state->mNumBuffersDecoded = 0;
        state->mIsAudio = isAudio;

        state->mCodec = MediaCodec::CreateByType(
                looper, mime.c_str(), false /* encoder */);

        CHECK(state->mCodec != NULL);

        err = state->mCodec->configure(
                format, isVideo ? surface : NULL,
                NULL /* crypto */,
                0 /* flags */);

        CHECK_EQ(err, (status_t)OK);

        state->mSignalledInputEOS = false;
        state->mSawOutputEOS = false;
    }

    CHECK(!stateByTrack.isEmpty());

    int64_t startTimeUs = android::ALooper::GetNowUs();
    int64_t startTimeRender = -1;

    for (size_t i = 0; i < stateByTrack.size(); ++i) {
        CodecState *state = &stateByTrack.editValueAt(i);

        sp<MediaCodec> codec = state->mCodec;

        CHECK_EQ((status_t)OK, codec->start());

        CHECK_EQ((status_t)OK, codec->getInputBuffers(&state->mInBuffers));
        CHECK_EQ((status_t)OK, codec->getOutputBuffers(&state->mOutBuffers));

        ALOGV("got %zu input and %zu output buffers",
              state->mInBuffers.size(), state->mOutBuffers.size());
    }

    bool sawInputEOS = false;

    for (;;) {
        if (!sawInputEOS) {
            size_t trackIndex;
            status_t err = extractor->getSampleTrackIndex(&trackIndex);

            if (err != OK) {
                ALOGV("saw input eos");
                sawInputEOS = true;
            } else {
                CodecState *state = &stateByTrack.editValueFor(trackIndex);

                ssize_t eventIndex = ozoHeadsetEvents.indexOfKey(state->mNumBuffersDecoded);
                if (eventIndex >= 0) {
                    sp<AMessage> message = new AMessage();
                    auto value = ozoHeadsetEvents.valueAt(eventIndex);
                    ALOGD("Headset event detected: %zu %s", state->mNumBuffersDecoded, value.c_str());
                    message->setString("vendor.ozoaudio.ls-widening.value", value);
                    state->mCodec->setParameters(message);
                }

                size_t index;
                err = state->mCodec->dequeueInputBuffer(&index, kTimeout);

                if (err == OK) {
                    ALOGV("filling input buffer %zu", index);

                    const sp<MediaCodecBuffer> &buffer = state->mInBuffers.itemAt(index);
                    sp<ABuffer> abuffer = new ABuffer(buffer->base(), buffer->capacity());

                    err = extractor->readSampleData(abuffer);
                    CHECK_EQ(err, (status_t)OK);
                    buffer->setRange(abuffer->offset(), abuffer->size());

                    int64_t timeUs;
                    err = extractor->getSampleTime(&timeUs);
                    CHECK_EQ(err, (status_t)OK);

                    uint32_t bufferFlags = 0;

                    err = state->mCodec->queueInputBuffer(
                            index,
                            0 /* offset */,
                            buffer->size(),
                            timeUs,
                            bufferFlags);

                    CHECK_EQ(err, (status_t)OK);

                    extractor->advance();
                } else {
                    CHECK_EQ(err, -EAGAIN);
                }
            }
        } else {
            for (size_t i = 0; i < stateByTrack.size(); ++i) {
                CodecState *state = &stateByTrack.editValueAt(i);

                if (!state->mSignalledInputEOS) {
                    size_t index;
                    status_t err =
                        state->mCodec->dequeueInputBuffer(&index, kTimeout);

                    if (err == OK) {
                        ALOGV("signalling input EOS on track %zu", i);

                        err = state->mCodec->queueInputBuffer(
                                index,
                                0 /* offset */,
                                0 /* size */,
                                0ll /* timeUs */,
                                MediaCodec::BUFFER_FLAG_EOS);

                        CHECK_EQ(err, (status_t)OK);

                        state->mSignalledInputEOS = true;
                    } else {
                        CHECK_EQ(err, -EAGAIN);
                    }
                }
            }
        }

        bool sawOutputEOSOnAllTracks = true;
        for (size_t i = 0; i < stateByTrack.size(); ++i) {
            CodecState *state = &stateByTrack.editValueAt(i);
            if (!state->mSawOutputEOS) {
                sawOutputEOSOnAllTracks = false;
                break;
            }
        }

        if (sawOutputEOSOnAllTracks) {
            break;
        }

        for (size_t i = 0; i < stateByTrack.size(); ++i) {
            CodecState *state = &stateByTrack.editValueAt(i);

            if (state->mSawOutputEOS) {
                continue;
            }

            size_t index;
            size_t offset;
            size_t size;
            int64_t presentationTimeUs;
            uint32_t flags;
            status_t err = state->mCodec->dequeueOutputBuffer(
                    &index, &offset, &size, &presentationTimeUs, &flags,
                    kTimeout);

            if (err == OK) {
                ALOGV("draining output buffer %zu, time = %lld us",
                      index, (long long)presentationTimeUs);

                if (waveWriter)
                {
                    sp<MediaCodecBuffer> outputBuffer;
                    auto err = state->mCodec->getOutputBuffer(index, &outputBuffer);
                    CHECK_EQ(err, (status_t) OK);

                    waveWriter->Append(outputBuffer->data(), outputBuffer->size());
                }

                ++state->mNumBuffersDecoded;
                state->mNumBytesDecoded += size;

                if (surface == NULL || !renderSurface) {
                    err = state->mCodec->releaseOutputBuffer(index);
                } else if (useTimestamp) {
                    if (startTimeRender == -1) {
                        // begin rendering 2 vsyncs (~33ms) after first decode
                        startTimeRender =
                                systemTime(SYSTEM_TIME_MONOTONIC) + 33000000
                                - (presentationTimeUs * 1000);
                    }
                    presentationTimeUs =
                            (presentationTimeUs * 1000) + startTimeRender;
                    err = state->mCodec->renderOutputBufferAndRelease(
                            index, presentationTimeUs);
                } else {
                    err = state->mCodec->renderOutputBufferAndRelease(index);
                }

                CHECK_EQ(err, (status_t)OK);

                if (flags & MediaCodec::BUFFER_FLAG_EOS) {
                    ALOGV("reached EOS on output.");

                    state->mSawOutputEOS = true;
                }
            } else if (err == INFO_OUTPUT_BUFFERS_CHANGED) {
                ALOGV("INFO_OUTPUT_BUFFERS_CHANGED");
                CHECK_EQ((status_t)OK,
                         state->mCodec->getOutputBuffers(&state->mOutBuffers));

                ALOGV("got %zu output buffers", state->mOutBuffers.size());
            } else if (err == INFO_FORMAT_CHANGED) {
                sp<AMessage> format;
                CHECK_EQ((status_t)OK, state->mCodec->getOutputFormat(&format));

                ALOGV("INFO_FORMAT_CHANGED: %s", format->debugString().c_str());

                if (!waveWriter && output) {
                    int32_t channelCount = 2;
                    int32_t sampleRate = 48000;
                    CHECK(format->findInt32("channel-count", &channelCount));
                    CHECK(format->findInt32("sample-rate", &sampleRate));
                    waveWriter = new WaveWriter(output, channelCount, sampleRate);
                }
            } else {
                CHECK_EQ(err, -EAGAIN);
            }

            ssize_t eventIndex = ozoEvents.indexOfKey(state->mNumBuffersDecoded);
            if (eventIndex >= 0) {
                sp<AMessage> message = new AMessage();
                message->setString(ozoOrientationTag, ozoEvents.valueAt(eventIndex));
                state->mCodec->setParameters(message);
            }
        }
    }

    int64_t elapsedTimeUs = android::ALooper::GetNowUs() - startTimeUs;

    for (size_t i = 0; i < stateByTrack.size(); ++i) {
        CodecState *state = &stateByTrack.editValueAt(i);

        CHECK_EQ((status_t)OK, state->mCodec->release());

        if (state->mIsAudio) {
            printf("track %zu: %lld bytes received. %.2f KB/sec\n",
                   i,
                   (long long)state->mNumBytesDecoded,
                   state->mNumBytesDecoded * 1E6 / 1024 / elapsedTimeUs);
        } else {
            printf("track %zu: %lld frames decoded, %.2f fps. %lld"
                    " bytes received. %.2f KB/sec\n",
                   i,
                   (long long)state->mNumBuffersDecoded,
                   state->mNumBuffersDecoded * 1E6 / elapsedTimeUs,
                   (long long)state->mNumBytesDecoded,
                   state->mNumBytesDecoded * 1E6 / 1024 / elapsedTimeUs);
        }
    }

    delete waveWriter;

    return 0;
}

int main(int argc, char **argv) {
    using namespace android;

    const char *me = argv[0];

    bool useAudio = false;
    bool useVideo = false;
    bool playback = false;
    bool useSurface = false;
    bool renderSurface = false;
    bool useTimestamp = false;
    bool isOzoAudio = false;
    const char* ozoGuidance = "none";
    const char* ozoOrientations = nullptr;
    const char* ozoHeadsets = nullptr;
    const char* output = nullptr;

    int res;
    while ((res = getopt(argc, argv, "havpSDRToH:O:g:l:")) >= 0) {
        switch (res) {
            case 'a':
            {
                useAudio = true;
                break;
            }
            case 'v':
            {
                useVideo = true;
                break;
            }
            case 'p':
            {
                playback = true;
                break;
            }
            case 'o':
            {
                isOzoAudio = true;
                break;
            }
            case 'O':
            {
                if(optarg) output = optarg;
                break;
            }
            case 'g':
            {
                if(optarg) ozoGuidance = optarg;
                break;
            }
            case 'H':
            {
                if(optarg) ozoHeadsets = optarg;
                break;
            }
            case 'l':
            {
                if(optarg) ozoOrientations = optarg;
                break;
            }
            case 'T':
            {
                useTimestamp = true;
                FALLTHROUGH_INTENDED;
            }
            case 'R':
            {
                renderSurface = true;
                FALLTHROUGH_INTENDED;
            }
            case 'S':
            {
                useSurface = true;
                break;
            }
            case '?':
            case 'h':
            default:
            {
                usage(me);
            }
        }
    }

    argc -= optind;
    argv += optind;

    if (argc != 1) {
        usage(me);
    }

    if (!useAudio && !useVideo) {
        useAudio = useVideo = true;
    }

    // Orientation events for Ozo audio decoding
    KeyedVector<size_t, AString> ozoEvents;
    parseOzoOrientations(ozoEvents, ozoOrientations);

    // Headset (on/off) events for Ozo audio decoding
    KeyedVector<size_t, AString> ozoHeadsetEvents;
    parseOzoOrientations(ozoHeadsetEvents, ozoHeadsets);

    ProcessState::self()->startThreadPool();

    sp<android::ALooper> looper = new android::ALooper;
    looper->start();

    sp<SurfaceComposerClient> composerClient;
    sp<SurfaceControl> control;
    sp<Surface> surface;

    if (playback || (useSurface && useVideo)) {
        composerClient = new SurfaceComposerClient;
        CHECK_EQ(composerClient->initCheck(), (status_t)OK);

        const sp<IBinder> display = SurfaceComposerClient::getInternalDisplayToken();
        CHECK(display != nullptr);

        DisplayConfig config;
        CHECK_EQ(SurfaceComposerClient::getActiveDisplayConfig(display, &config), NO_ERROR);

        const ui::Size& resolution = config.resolution;
        const ssize_t displayWidth = resolution.getWidth();
        const ssize_t displayHeight = resolution.getHeight();

        ALOGV("display is %zd x %zd\n", displayWidth, displayHeight);

        control = composerClient->createSurface(
                String8("A Surface"),
                displayWidth,
                displayHeight,
                PIXEL_FORMAT_RGB_565,
                0);

        CHECK(control != NULL);
        CHECK(control->isValid());

        SurfaceComposerClient::Transaction{}
                 .setLayer(control, INT_MAX)
                 .show(control)
                 .apply();

        surface = control->getSurface();
        CHECK(surface != NULL);
    }

    if (playback) {
        sp<SimplePlayer> player = new SimplePlayer;
        looper->registerHandler(player);

        player->setDataSource(argv[0]);
        player->setSurface(surface->getIGraphicBufferProducer());
        player->start();
        sleep(60);
        player->stop();
        player->reset();
    } else {
        decode(looper, argv[0], useAudio, useVideo, surface, renderSurface,
               useTimestamp, output, isOzoAudio,
                ozoGuidance, ozoEvents, ozoHeadsetEvents);
    }

    if (playback || (useSurface && useVideo)) {
        composerClient->dispose();
    }

    looper->stop();

    return 0;
}
