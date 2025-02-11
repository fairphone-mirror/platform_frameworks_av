#ifndef FPCAMERHELPER
#define FPCAMERHELPER

#include "utils/String16.h"
#include <cutils/properties.h>
#include <utils/Mutex.h>
#include "common/CameraDeviceBase.h"
#include <camera/camera2/SubmitInfo.h>
#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <condition_variable>
#include "utils/ClientManager.h"

#ifndef CONSTEXPR
#if __cplusplus >= 201103L
#define CONSTEXPR constexpr
#else
#define CONSTEXPR
#endif
#endif
#include <aidl/vendor/tcl/camera/algoservice/ITctCameraAlgoService.h>
#include <cutils/properties.h>
#include "CameraService.h"
#include "api1/Camera2Client.h"
#include "api1/client2/Parameters.h"
using aidl::vendor::tcl::camera::algoservice::ITctCameraAlgoService;

#define DUMMY_FRAME 1
namespace android{

/**
 * @author hongzhang
 **/
class FPCameraHelper {
public:
    static int overrideResult(uint32_t frameNumber, CameraMetadata& result);

    static int MergeMetaData(const camera_metadata_t* pMetadata1, CameraMetadata&  dstMetadata);
    static std::shared_ptr<ITctCameraAlgoService> getAlgoService();
};

struct BufferHolder {
    uint64_t frameNumber;
    sp<ANativeWindowBuffer> anwBuffer;
    int anwReleaseFence;
    uint64_t usage;

    BufferHolder() = default;
    BufferHolder(uint64_t fn, ANativeWindowBuffer* anwb, int rf, uint64_t us) :
            frameNumber(fn), anwBuffer(anwb),
            anwReleaseFence(rf), usage(us) {}
};

/**
 * @brief this class just used for multiple stream synchronization of CameraAlgoService
 */
class FrameSynchronizer {
public:
    // streamid -> single frame
    using FrameMap = std::unordered_map<int32_t, BufferHolder>;
    using StreamInfo = std::pair<int32_t, sp<Surface>>;

    /**
     * @brief get the instance of FrameSynchronizer
     *
     * @return
     */
    static auto getInstance() -> std::shared_ptr<FrameSynchronizer>;

    FrameSynchronizer();
    ~FrameSynchronizer();

    /**
     * @brief use for speed up stream configuration
     *
     * @param id: raw pointer of sp<Surface>, we use it as the unique identifier for a stream
     */
    void registerSurface(void* id) { mRegisteredSurfaceId.insert(id); }
    bool isRegisteredSurface(void* id) { return mRegisteredSurfaceId.count(id); };

    /**
     * @brief initialize the FrameSynchronizer by configurations from OEM
     *
     * @param frameCount
     */
    void initialize(const std::initializer_list<StreamInfo>& li);

    /**
     * @brief
     */
    void uninitialize();

    /**
     * @brief
     *
     * @return true: need multiple stream synchronization
     *         false: not a multiple stream
     */
    bool needFrameSync(int32_t streamId);

    /**
     * @brief get main stream id if of a composite stream
     *
     * @return
     */
    int32_t getMainStreamId() { return mMainStreamId; }

    /**
     * @brief
     *
     * @param frameNumber: input frameNumber
     * @param streamId: input stream id
     * @param frame: input frame
     *
     * @return true: push frame successful
     *         false: push frame failed, caller need to callback this frame (eg: invoke queueBuffer) immediately
     */
    bool pushFrame(uint64_t frameNumber, int32_t streamId, BufferHolder&& frame);

    /**
     * @brief
     *
     * @param frameNumber: input frameNumber
     *
     * @return if there has enough frames for specified frameNumber, return all there frames; or return a empty container
     */
    auto popFrames(uint64_t frameNumber) -> FrameMap;

    /**
     * @brief
     *
     * @param frameMap
     *
     * @return
     */
    void releaseFrames(FrameMap&& frameMap);

private:
    bool        mIsInit;
    std::mutex  mMutex;
    std::condition_variable
                mFrameReadyCond;
    uint32_t    mStreamCount;
    int32_t     mMainStreamId;
    std::unordered_set<void *>
                mRegisteredSurfaceId;
    std::unordered_map<uint64_t, FrameMap>
                mCachedFrames;
    std::unordered_map<uint64_t, uint32_t>
                mDroppedFrameCounts;
    std::unordered_map<int32_t, sp<Surface>>
                mRegisteredSurfaces;
};

}

#endif
