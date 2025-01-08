/*
 * Copyright (C) 2016-2018 The Android Open Source Project
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

#ifndef ANDROID_SERVERS_CAMERA3_FP_OUTPUT_STREAM_H
#define ANDROID_SERVERS_CAMERA3_FP_OUTPUT_STREAM_H

#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoService.h>
#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoSession.h>
#include <array>
#include <cstdint>
#include <memory>
#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
#include "device3/Camera3OutputStream.h"
#include "fputils/FPCameraHelper.h"

using aidl::vendor::tcl::camera::algoservice::ITctCameraAlgoService;
using aidl::vendor::tcl::camera::algoservice::ITctCameraAlgoSession;

namespace android {

namespace camera3 {

class Camera3FPOutputStream :
        public Camera3OutputStream,
        public Camera3StreamBufferListener {
public:
    static bool negotiate(const std::string cameraId, int streamId, const std::vector<sp<Surface>>& consumers, bool hasDeferredConsumer,
        uint32_t width, uint32_t height, uint32_t format, uint64_t consumerUsage,
        android_dataspace dataSpace, camera_stream_rotation_t rotation,
        const std::string& physicalCameraId, int streamSetId, int32_t &extraBufferCnt, int32_t scenetype);
		static int32_t getscenetype(const CameraMetadata& sessionParams, metadata_vendor_id_t VendorTagId);
    /**
     * Set up a stream for formats that have 2 dimensions, such as RAW and YUV.
     * A valid stream set id needs to be set to support buffer sharing between multiple
     * streams.
     */
    Camera3FPOutputStream(
            const std::string cameraId, int id, sp<Surface> consumer,
            uint32_t width, uint32_t height, int format,
            android_dataspace dataSpace, camera_stream_rotation_t rotation,
            nsecs_t timestampOffset, const std::string& physicalCameraId,
            const std::unordered_set<int32_t> &sensorPixelModesUsed, IPCTransport transport, int32_t extraBufferCnt,
            int32_t scenetype, CameraMetadata* characteristics, int setId = CAMERA3_STREAM_SET_ID_INVALID, bool isMultiResolution = false);

    Camera3FPOutputStream(
            const std::string cameraId, int id,
            uint32_t width, uint32_t height, int format, uint64_t consumerUsage,
            android_dataspace dataSpace, camera_stream_rotation_t rotation,
            nsecs_t timestampOffset, const std::string& physicalCameraId,
            const std::unordered_set<int32_t> &sensorPixelModesUsed, IPCTransport transport, int32_t extraBufferCnt,
            int32_t scenetype, CameraMetadata* characteristics, int setId = CAMERA3_STREAM_SET_ID_INVALID, bool isMultiResolution = false);

    virtual ~Camera3FPOutputStream();
    static int32_t CheckStreamFeature();
private:

    class FPOutputStreamThread : public Thread {
    public:
        explicit FPOutputStreamThread(Camera3FPOutputStream* context) : mContext(context) {}
        ~FPOutputStreamThread() {}
        bool needStop() {
            return exitPending();
        }
    private:
        virtual bool threadLoop() override {
            return mContext->doThreadLoop();
        }
        Camera3FPOutputStream* mContext;
    };

    bool doThreadLoop();
    void stopThread();
    // Buffer was acquired by the HAL
    virtual void onBufferAcquired(const BufferInfo& bufferInfo);
    // Buffer was released by the HAL
    virtual void onBufferReleased(const BufferInfo& bufferInfo);
    // Notify about incoming buffer request frame number
    virtual void onBufferRequestForFrameNumber(uint64_t frameNumber, int streamId,
            const CameraMetadata& settings);

    /**
     * Return if this output stream is for video encoding.
     */
    bool isVideoStream();

    virtual status_t getEndpointUsage(uint64_t *usage);

    virtual status_t queueBufferToConsumer(sp<ANativeWindow>& consumer,
            ANativeWindowBuffer* buffer, int anwReleaseFence,
            const std::vector<size_t>& uniqueSurfaceIds);
#if 0
    status_t returnBufferCheckedLocked(
            const camera_stream_buffer &buffer,
            nsecs_t timestamp,
            bool output,
            const std::vector<size_t>& surface_ids,
            /*out*/
            sp<Fence> *releaseFenceOut);
#endif
    status_t returnBuffer(const camera_stream_buffer &buffer,
            nsecs_t timestamp, nsecs_t readoutTimestamp, bool timestampIncreasing,
            const std::vector<size_t>& surface_ids = std::vector<size_t>(),
            uint64_t frameNumber = 0, int32_t transform = -1);


    // Stream ID -> OutputConfiguration. Used for looking up Surface by stream/surface index
    KeyedVector<ANativeWindowBuffer *, uint64_t> mBufferFrameNum;
    // Frame number to capture result map of partial pending request results.
    std::unordered_map<uint64_t, CameraMetadata> mPendingCaptureResults;
    std::shared_ptr<ITctCameraAlgoService>       mTctCameraAlgoService;
    std::shared_ptr<ITctCameraAlgoSession>       mAlgoSession;
    static constexpr size_t kStreamExtraBuffer = 2;
    mutable Mutex mLock;
    Condition mBufferCond;

    static int32_t mHasStreamFeature;
    std::queue<BufferHolder> mPendingBuffers;
    static constexpr nsecs_t kWaitDuration = 5000000LL; // 50ms
    sp<FPOutputStreamThread> mThread;
}; // class Camera3FPOutputStream

} // namespace camera3

} // namespace android

#endif // ANDROID_SERVERS_CAMERA3_FP_OUTPUT_STREAM_H
