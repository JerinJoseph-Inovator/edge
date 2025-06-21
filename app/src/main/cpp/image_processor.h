#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <opencv2/core.hpp>

void processFrame(const cv::Mat& input, cv::Mat& output);

#endif // IMAGE_PROCESSOR_H
