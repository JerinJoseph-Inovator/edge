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

// Render mode constants (must match Java enum order)
enum RenderMode {
    RAW_CAMERA = 0,
    EDGE_DETECTION = 1,
    GRAYSCALE = 2,
    DEFAULT = 3,
    INSET = 4,
    BORDER_FIX = 5
};

// Global frames storage
static cv::Mat rawFrame;        // Original camera data (BGR)
static cv::Mat processedFrame;  // OpenCV processed data
static cv::Mat grayscaleFrame;  // Grayscale version
static std::mutex frameMutex;
static RenderMode currentRenderMode = EDGE_DETECTION; // Default to edge detection

// Helper function to rotate cv::Mat based on rotation angle
cv::Mat rotateFrame(const cv::Mat& frame, int rotation) {
    cv::Mat rotated;

    switch (rotation) {
        case 0:
            // No rotation needed
            rotated = frame.clone();
            break;
        case 90:
            cv::rotate(frame, rotated, cv::ROTATE_90_CLOCKWISE);
            break;
        case 180:
            cv::rotate(frame, rotated, cv::ROTATE_180);
            break;
        case 270:
            cv::rotate(frame, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
            break;
        default:
            LOGE("❌ Unsupported rotation angle: %d, using original frame", rotation);
            rotated = frame.clone();
            break;
    }

    LOGI("✅ Frame rotated by %d degrees: %dx%d -> %dx%d",
         rotation, frame.cols, frame.rows, rotated.cols, rotated.rows);
    return rotated;
}

// Common frame processing logic
void processFrameInternal(jbyte* frameData, jint width, jint height, jint rotation = 0) {
    LOGI("🔄 [STEP 1] Processing frame with size: %dx%d, rotation: %d°", width, height, rotation);

    if (!frameData) {
        LOGE("❌ [STEP 1] frameData is null — skipping");
        return;
    }
    LOGI("✅ [STEP 1] frameData obtained successfully");

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
        return;
    }

    // Step 2: Apply rotation if needed
    cv::Mat rotatedBgr = bgr;
    if (rotation != 0) {
        rotatedBgr = rotateFrame(bgr, rotation);
        LOGI("✅ [STEP 2B] Frame rotated by %d degrees", rotation);
    }

    // Step 3: Store raw frame and create all variants
    std::lock_guard<std::mutex> lock(frameMutex);

    // Store raw camera data (rotated)
    rawFrame = rotatedBgr.clone();
    LOGI("✅ [STEP 3A] Raw frame stored: %dx%d", rawFrame.cols, rawFrame.rows);

    // Create grayscale version
    try {
        cv::cvtColor(rotatedBgr, grayscaleFrame, cv::COLOR_BGR2GRAY);
        cv::cvtColor(grayscaleFrame, grayscaleFrame, cv::COLOR_GRAY2BGR); // Convert back to 3-channel for OpenGL
        LOGI("✅ [STEP 3B] Grayscale frame created: %dx%d", grayscaleFrame.cols, grayscaleFrame.rows);
    } catch (const cv::Exception& e) {
        LOGE("❌ [STEP 3B] Grayscale conversion failed: %s", e.what());
        grayscaleFrame = rotatedBgr.clone(); // Fallback to raw
    }

    // Create edge detection version
    try {
        processFrame(rotatedBgr, processedFrame);  // Your existing OpenCV processing
        LOGI("✅ [STEP 3C] Edge detection completed: %dx%d", processedFrame.cols, processedFrame.rows);
    } catch (const std::exception& e) {
        LOGE("❌ [STEP 3C] processFrame() crashed: %s", e.what());
        processedFrame = rotatedBgr.clone(); // Fallback to raw
    } catch (...) {
        LOGE("❌ [STEP 3C] processFrame() crashed with unknown exception");
        processedFrame = rotatedBgr.clone(); // Fallback to raw
    }

    LOGI("✅ [STEP 4] All frame variants ready - processFrame complete");
}

// Original JNI function (backward compatibility)
extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_nativebridge_NativeBridge_nativeProcessFrame(JNIEnv *env, jclass clazz,
                                                                   jbyteArray frameData_,
                                                                   jint width, jint height) {
    jbyte* frameData = env->GetByteArrayElements(frameData_, nullptr);
    processFrameInternal(frameData, width, height, 0); // No rotation
    env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
}

// New JNI function with rotation support
extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_nativebridge_NativeBridge_nativeProcessFrameWithRotation(JNIEnv *env, jclass clazz,
                                                                               jbyteArray frameData_,
                                                                               jint width, jint height, jint rotation) {
    jbyte* frameData = env->GetByteArrayElements(frameData_, nullptr);
    processFrameInternal(frameData, width, height, rotation);
    env->ReleaseByteArrayElements(frameData_, frameData, JNI_ABORT);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_nativebridge_NativeBridge_nativeCleanup(JNIEnv *env, jclass clazz) {
    LOGI(">> JNI cleanup called");

    std::lock_guard<std::mutex> lock(frameMutex);
    rawFrame.release();
    processedFrame.release();
    grayscaleFrame.release();

    LOGI("✅ Native cleanup completed");
}

// FIXED: Add the missing setRenderModeNative function that GLRenderer calls
extern "C"
JNIEXPORT void JNICALL
Java_com_example_edge_renderer_GLRenderer_setRenderModeNative(JNIEnv *env, jobject thiz, jint mode) {
    currentRenderMode = static_cast<RenderMode>(mode);
    LOGI("🔄 Render mode changed to: %d (%s)", mode,
         mode == 0 ? "RAW_CAMERA" :
         mode == 1 ? "EDGE_DETECTION" :
         mode == 2 ? "GRAYSCALE" :
         mode == 3 ? "DEFAULT" :
         mode == 4 ? "INSET" :
         mode == 5 ? "BORDER_FIX" : "UNKNOWN");
}

// This function is called by your OpenGL renderer to get the right frame
cv::Mat getLatestFrameForRender() {
    static int debugCounter = 0;
    static cv::Mat fallbackFrame;

    // Initialize fallback frame once
    if (fallbackFrame.empty()) {
        fallbackFrame = cv::Mat::zeros(480, 640, CV_8UC3);
        // Make it clearly visible for debugging - blue frame
        fallbackFrame.setTo(cv::Scalar(255, 0, 0)); // Blue in BGR
        LOGI("✅ [RENDER] Created blue fallback frame: 640x480");
    }

    std::lock_guard<std::mutex> lock(frameMutex);

    cv::Mat frameToReturn;

    switch (currentRenderMode) {
        case RAW_CAMERA:
            if (!rawFrame.empty()) {
                frameToReturn = rawFrame.clone();
                LOGI("✅ [RENDER] [%d] Returning RAW camera frame %dx%d", debugCounter++, frameToReturn.cols, frameToReturn.rows);
            } else {
                frameToReturn = fallbackFrame.clone();
                LOGE("❌ [RENDER] [%d] Raw frame empty, using blue fallback", debugCounter++);
            }
            break;

        case GRAYSCALE:
            if (!grayscaleFrame.empty()) {
                frameToReturn = grayscaleFrame.clone();
                LOGI("✅ [RENDER] [%d] Returning GRAYSCALE frame %dx%d", debugCounter++, frameToReturn.cols, frameToReturn.rows);
            } else {
                frameToReturn = fallbackFrame.clone();
                LOGE("❌ [RENDER] [%d] Grayscale frame empty, using blue fallback", debugCounter++);
            }
            break;

        case EDGE_DETECTION:
        case DEFAULT:
        case INSET:
        case BORDER_FIX:
        default:
            if (!processedFrame.empty()) {
                frameToReturn = processedFrame.clone();
                LOGI("✅ [RENDER] [%d] Returning EDGE_DETECTION/DEFAULT frame %dx%d", debugCounter++, frameToReturn.cols, frameToReturn.rows);
            } else {
                frameToReturn = fallbackFrame.clone();
                LOGE("❌ [RENDER] [%d] Processed frame empty, using blue fallback", debugCounter++);
            }
            break;
    }

    return frameToReturn;
}