cmake_minimum_required(VERSION 3.10.2)
project("edge")

# 🧠 Set OpenCV path — make sure this path is valid in your setup
# This should point to OpenCV-android-sdk/sdk/native/jni
set(OpenCV_DIR "${CMAKE_SOURCE_DIR}/../../../../OpenCV-android-sdk/sdk/native/jni")
find_package(OpenCV REQUIRED)

# 🧩 Add your native source files
add_library(edge SHARED
        native-lib.cpp
        image_processor.cpp
        opengl_renderer.cpp
)

# 🔍 Include OpenCV headers
target_include_directories(edge
        PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)

# 🔗 Link OpenCV + native system libraries
target_link_libraries(edge
        ${OpenCV_LIBS}       # OpenCV core libraries
        log                  # For __android_log_print
        android              # Android NDK native APIs
        GLESv2               # OpenGL ES 2.0 support
)
