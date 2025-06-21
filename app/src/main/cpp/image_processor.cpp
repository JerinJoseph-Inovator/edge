#include "image_processor.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <android/log.h>

#define LOG_TAG "ImageProcessor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

void processFrame(const cv::Mat& input, cv::Mat& output) {
    if (input.empty()) {
        LOGE("Input frame is empty!");
        return;
    }

    LOGI("Processing frame: %dx%d, type=%d", input.cols, input.rows, input.type());

    try {
        // ✅ Convert to grayscale
        cv::Mat gray;
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

        // ✅ Apply Canny edge detection
        cv::Mat edges;
        cv::Canny(gray, edges, 100, 200);

        // ✅ Convert back to 3-channel for OpenGL rendering
        cv::cvtColor(edges, output, cv::COLOR_GRAY2BGR);

        LOGI("Frame processed successfully. Output size: %dx%d", output.cols, output.rows);
    } catch (const cv::Exception& e) {
        LOGE("OpenCV error in processFrame: %s", e.what());
        output = input.clone(); // fallback to original
    }
}
