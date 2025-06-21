// Replace your native-lib.cpp with this debug version temporarily

#include <jni.h>
#include <string>
#include <android/log.h>
#include <opencv2/opencv.hpp>
#include "image_processor.h"
#include "opengl_renderer.h"
#include <mutex>

#define LOG_TAG "NativeBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global frame shared with OpenGL
static cv::Mat latestFrame;
static std::mutex frameMutex;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_nativebridge_NativeBridge_nativeProcessFrame(JNIEnv *env, jclass clazz,
                                                                   jbyteArray frameData_,
                                                                   jint width, jint height) {
    LOGI("🔄 [STEP 1] JNI processFrame called with size: %dx%d", width, height);

    jbyte* frameData = env->GetByteArrayElements(frameData_, nullptr);
    if (!frameData) {
        LOGE("❌ [STEP 1] JNI frameData is null — skipping");
        return;
    }
    LOGI("✅ [STEP 1] JNI frameData obtained successfully");

    // Step 1: Convert NV21 YUV to BGR
    LOGI("🔄 [STEP 2] Starting YUV to BGR conversion...");
    int yuvHeight = height + height / 2;
    cv::Mat yuv(yuvHeight, width, CV_8UC1, reinterpret_cast<unsigned char*>(frameData));
    cv::Mat bgr;

    try {
        cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);
        LOGI("✅ [STEP 2] cvtColor success: BGR size = %dx%d", bgr.cols, bgr.rows);
    } catch (const cv::Exception& e) {
        LOGE("❌ [STEP 2] OpenCV YUV2BGR_NV21 failed: %s", e.what());
        env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
        return;
    }

    // Step 2: Apply image processing
    LOGI("🔄 [STEP 3] Starting processFrame()...");
    cv::Mat processed;

    try {
        processFrame(bgr, processed);  // ⚠️ This could be where it crashes!
        LOGI("✅ [STEP 3] processFrame() completed successfully");
    } catch (const std::exception& e) {
        LOGE("❌ [STEP 3] processFrame() crashed: %s", e.what());
        env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
        return;
    } catch (...) {
        LOGE("❌ [STEP 3] processFrame() crashed with unknown exception");
        env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
        return;
    }

    if (processed.empty()) {
        LOGE("❌ [STEP 3] processFrame() returned empty frame!");
        env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
        return;
    } else {
        LOGI("✅ [STEP 3] Processed frame ready: %dx%d", processed.cols, processed.rows);
    }

    // Step 3: Store frame for OpenGL (thread-safe)
    LOGI("🔄 [STEP 4] Storing frame for OpenGL...");
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame = processed.clone();
    }
    LOGI("✅ [STEP 4] Frame stored successfully");

    // Step 4: Release JNI memory
    env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
    LOGI("✅ [STEP 5] JNI memory released - processFrame complete");
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_nativebridge_NativeBridge_nativeCleanup(JNIEnv *env, jclass clazz) {
    LOGI(">> JNI cleanup called");

    // Clean up the global frame
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame.release();
    }

    LOGI("✅ Native cleanup completed");
}

// Extern used by OpenGL to fetch latest frame
cv::Mat getLatestFrameForRender() {
    static int debugCounter = 0;
    static cv::Mat fallbackFrame;

    LOGI("🔄 [RENDER] getLatestFrameForRender() called [%d]", debugCounter);

    // Initialize fallback frame once
    if (fallbackFrame.empty()) {
        fallbackFrame = cv::Mat::zeros(480, 640, CV_8UC3);
        LOGI("✅ [RENDER] Created fallback frame: 640x480");
    }

    std::lock_guard<std::mutex> lock(frameMutex);
    if (latestFrame.empty()) {
        LOGE("❌ [RENDER] [%d] latestFrame is empty — returning fallback", debugCounter++);
        return fallbackFrame.clone();
    }

    LOGI("✅ [RENDER] [%d] Returning frame %dx%d", debugCounter++, latestFrame.cols, latestFrame.rows);
    return latestFrame.clone();
}