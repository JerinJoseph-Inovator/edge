#include "opengl_renderer.h"
#include <GLES2/gl2.h>
#include <opencv2/opencv.hpp>
#include <android/log.h>
#include <vector>
#include <atomic>
#include <algorithm>

#define LOG_TAG "OpenGLRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// External method to get processed OpenCV frame (thread-safe double buffer)
cv::Mat getLatestFrameForRender();

static GLuint textureId = 0;
static GLuint program = 0;
static GLint posLoc = -1, texLoc = -1, samplerLoc = -1;
static int texWidth = 1024, texHeight = 512;

// Pre-allocated upload buffer
static std::vector<uint8_t> pixelBuffer;

// CRITICAL FIX #1: Adjust texture coordinates to avoid edge sampling artifacts
// Instead of 0.0-1.0, use slightly inset coordinates to avoid boundary issues
static const GLfloat vertices[] = {
        // Position     // Texture coords (slightly inset to avoid edge artifacts)
        -1.0f, -1.0f,   0.0f, 0.0f,    // Bottom-left
        1.0f, -1.0f,   1.0f, 0.0f,    // Bottom-right
        -1.0f,  1.0f,   0.0f, 1.0f,    // Top-left
        1.0f,  1.0f,   1.0f, 1.0f     // Top-right
};

// Alternative: Inset texture coordinates to avoid edge sampling
static const GLfloat verticesInset[] = {
        // Position     // Texture coords (inset by half pixel)
        -1.0f, -1.0f,   0.5f/1024.0f, 0.5f/512.0f,          // Bottom-left
        1.0f, -1.0f,   (1024.0f-0.5f)/1024.0f, 0.5f/512.0f, // Bottom-right
        -1.0f,  1.0f,   0.5f/1024.0f, (512.0f-0.5f)/512.0f,  // Top-left
        1.0f,  1.0f,   (1024.0f-0.5f)/1024.0f, (512.0f-0.5f)/512.0f // Top-right
};

// Simple passthrough shaders with high precision
const char* vertexShaderSrc = R"(
attribute vec2 a_Position;
attribute vec2 a_TexCoord;
varying highp vec2 v_TexCoord;
void main() {
    gl_Position = vec4(a_Position, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
}
)";

const char* fragmentShaderSrc = R"(
precision highp float;
varying highp vec2 v_TexCoord;
uniform sampler2D u_Texture;
void main() {
    gl_FragColor = texture2D(u_Texture, v_TexCoord);
}
)";

// Debug: Alternative fragment shader for testing
const char* fragmentShaderDebugSrc = R"(
precision highp float;
varying highp vec2 v_TexCoord;
uniform sampler2D u_Texture;
void main() {
    // Sample with explicit LOD to avoid mipmap issues
    gl_FragColor = texture2D(u_Texture, v_TexCoord, 0.0);
}
)";

static void checkGLError(const char* operation) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("GL Error after %s: 0x%x", operation, error);
    }
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(s, 512, nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void initGL() {
    // CRITICAL FIX #2: Disable dithering which can cause line artifacts
    glDisable(GL_DITHER);
    checkGLError("disable dither");

    // Compile & link program
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);

    if (vs == 0 || fs == 0) {
        LOGE("Failed to compile shaders");
        return;
    }

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char buf[512];
        glGetProgramInfoLog(program, 512, nullptr, buf);
        LOGE("Program link error: %s", buf);
        glDeleteProgram(program);
        program = 0;
        return;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    // Cache locations
    posLoc     = glGetAttribLocation(program, "a_Position");
    texLoc     = glGetAttribLocation(program, "a_TexCoord");
    samplerLoc = glGetUniformLocation(program, "u_Texture");

    if (posLoc == -1 || texLoc == -1 || samplerLoc == -1) {
        LOGE("Failed to get shader locations: pos=%d, tex=%d, sampler=%d",
             posLoc, texLoc, samplerLoc);
        return;
    }

    // Create texture
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    checkGLError("glBindTexture");

    // CRITICAL FIX #3: Try NEAREST filtering to eliminate interpolation artifacts
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    checkGLError("texture parameters");

    // Initialize with empty texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    checkGLError("glTexImage2D");

    // Pre-allocate buffer
    pixelBuffer.resize(texWidth * texHeight * 4);
    std::fill(pixelBuffer.begin(), pixelBuffer.end(), 0);

    LOGI("initGL complete: NEAREST filtering, dithering disabled");
}

void initGLLinear() {
    // Alternative init with LINEAR filtering for comparison
    glDisable(GL_DITHER);

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);

    if (vs == 0 || fs == 0) return;

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char buf[512];
        glGetProgramInfoLog(program, 512, nullptr, buf);
        LOGE("Program link error: %s", buf);
        return;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);

    posLoc     = glGetAttribLocation(program, "a_Position");
    texLoc     = glGetAttribLocation(program, "a_TexCoord");
    samplerLoc = glGetUniformLocation(program, "u_Texture");

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // LINEAR filtering with careful setup
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    pixelBuffer.resize(texWidth * texHeight * 4);
    std::fill(pixelBuffer.begin(), pixelBuffer.end(), 0);

    LOGI("initGLLinear complete: LINEAR filtering");
}

void resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    checkGLError("glViewport");
}

// CRITICAL FIX #4: Alternative rendering with inset texture coordinates
void renderGLInset() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    cv::Mat frame = getLatestFrameForRender();
    if (frame.empty()) return;

    // Process frame
    cv::Mat rgba;
    switch (frame.channels()) {
        case 1: cv::cvtColor(frame, rgba, cv::COLOR_GRAY2RGBA); break;
        case 3: cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA); break;
        case 4: frame.copyTo(rgba); break;
        default: return;
    }

    if (!rgba.isContinuous()) {
        rgba = rgba.clone();
    }

    if (rgba.cols != texWidth || rgba.rows != texHeight) {
        cv::resize(rgba, rgba, cv::Size(texWidth, texHeight));
    }

    // Upload texture
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgba.cols, rgba.rows,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);
    checkGLError("texture upload");

    // Render with inset coordinates
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(samplerLoc, 0);

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);

    // Use inset vertices to avoid edge sampling
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), verticesInset);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), verticesInset+2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGLError("draw arrays");

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);
}

// CRITICAL FIX #5: Clear texture borders to black before upload
void renderGLBorderFix() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    cv::Mat frame = getLatestFrameForRender();
    if (frame.empty()) return;

    cv::Mat rgba;
    switch (frame.channels()) {
        case 1: cv::cvtColor(frame, rgba, cv::COLOR_GRAY2RGBA); break;
        case 3: cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA); break;
        case 4: frame.copyTo(rgba); break;
        default: return;
    }

    if (!rgba.isContinuous()) {
        rgba = rgba.clone();
    }

    if (rgba.cols != texWidth || rgba.rows != texHeight) {
        cv::resize(rgba, rgba, cv::Size(texWidth, texHeight));
    }

    // CRITICAL: Clear border pixels to black to prevent edge artifacts
    // Top and bottom rows
    cv::rectangle(rgba, cv::Rect(0, 0, rgba.cols, 1), cv::Scalar(0,0,0,255), -1);
    cv::rectangle(rgba, cv::Rect(0, rgba.rows-1, rgba.cols, 1), cv::Scalar(0,0,0,255), -1);
    // Left and right columns
    cv::rectangle(rgba, cv::Rect(0, 0, 1, rgba.rows), cv::Scalar(0,0,0,255), -1);
    cv::rectangle(rgba, cv::Rect(rgba.cols-1, 0, 1, rgba.rows), cv::Scalar(0,0,0,255), -1);

    // Upload and render
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgba.cols, rgba.rows,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(samplerLoc, 0);

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), vertices);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), vertices+2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);
}

// Original render function (now with debug info)
// Replace your renderGL() function with this crash-safe version

void renderGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // CRITICAL FIX: Wrap frame access in try-catch
    cv::Mat frame;
    try {
        frame = getLatestFrameForRender();
        if (frame.empty()) {
            LOGI("Empty frame received - skipping render");
            return;
        }
    } catch (const std::exception& e) {
        LOGE("❌ Exception getting frame: %s", e.what());
        return;
    } catch (...) {
        LOGE("❌ Unknown exception getting frame");
        return;
    }

    // CRITICAL FIX: Validate frame data before processing
    if (frame.data == nullptr) {
        LOGE("❌ Frame data is null");
        return;
    }

    if (frame.cols <= 0 || frame.rows <= 0) {
        LOGE("❌ Invalid frame dimensions: %dx%d", frame.cols, frame.rows);
        return;
    }

    // Step 1: Convert to RGBA (OpenGL expects 4 channels)
    cv::Mat rgba;
    try {
        switch (frame.channels()) {
            case 1:
                cv::cvtColor(frame, rgba, cv::COLOR_GRAY2RGBA);
                break;
            case 3:
                cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
                break;
            case 4:
                frame.copyTo(rgba);
                break;
            default:
                LOGE("Unsupported frame channel count: %d", frame.channels());
                return;
        }
    } catch (const cv::Exception& e) {
        LOGE("❌ OpenCV color conversion failed: %s", e.what());
        return;
    }

    // Step 2: Make continuous (required by OpenGL)
    if (!rgba.isContinuous()) {
        try {
            rgba = rgba.clone();
        } catch (const std::exception& e) {
            LOGE("❌ Failed to clone RGBA frame: %s", e.what());
            return;
        }
    }

    // Step 3: Resize to texture size
    if (rgba.cols != texWidth || rgba.rows != texHeight) {
        try {
            cv::resize(rgba, rgba, cv::Size(texWidth, texHeight));
        } catch (const cv::Exception& e) {
            LOGE("❌ OpenCV resize failed: %s", e.what());
            return;
        }
    }

    // CRITICAL FIX: Validate RGBA data
    if (rgba.data == nullptr) {
        LOGE("❌ RGBA data is null after processing");
        return;
    }

    // Step 4: Safe upload to GPU
    size_t actualBytes = rgba.total() * rgba.elemSize();
    size_t expectedBytes = texWidth * texHeight * 4;

    if (actualBytes != expectedBytes) {
        LOGE("Size mismatch: got %zu, expected %zu", actualBytes, expectedBytes);
        return;
    }

    // CRITICAL FIX: Ensure pixel buffer is properly sized
    if (pixelBuffer.size() != expectedBytes) {
        LOGE("❌ Pixel buffer size mismatch: %zu vs %zu", pixelBuffer.size(), expectedBytes);
        try {
            pixelBuffer.resize(expectedBytes);
        } catch (const std::exception& e) {
            LOGE("❌ Failed to resize pixel buffer: %s", e.what());
            return;
        }
    }

    // CRITICAL FIX: Safe memory copy with bounds checking
    try {
        if (actualBytes <= pixelBuffer.size()) {
            std::memcpy(pixelBuffer.data(), rgba.data, actualBytes);
        } else {
            LOGE("❌ Buffer overflow prevented: %zu > %zu", actualBytes, pixelBuffer.size());
            return;
        }
    } catch (const std::exception& e) {
        LOGE("❌ Memory copy failed: %s", e.what());
        return;
    }

    // Step 5: OpenGL operations with error checking
    glBindTexture(GL_TEXTURE_2D, textureId);
    checkGLError("bind texture");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    checkGLError("pixel store");

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
    checkGLError("texture upload");

    // Step 6: Render
    glUseProgram(program);
    checkGLError("use program");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(samplerLoc, 0);
    checkGLError("bind sampler");

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);

    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGLError("draw arrays");

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);

    // LOGI("✅ Frame rendered successfully");
}

void cleanupGL() {
    if (textureId) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
    pixelBuffer.clear();
    pixelBuffer.shrink_to_fit();
    LOGI("OpenGL cleanup complete");
}

#include <jni.h>
extern "C" {
JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_initGLNative(JNIEnv*, jobject) {
    initGL(); // NEAREST filtering, dithering disabled
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_initGLLinearNative(JNIEnv*, jobject) {
    initGLLinear(); // LINEAR filtering for comparison
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_resizeGLNative(JNIEnv*, jobject, jint w, jint h) {
    resizeGL(w, h);
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_renderFrameNative(JNIEnv*, jobject) {
    renderGL(); // Original method with debug
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_renderFrameInsetNative(JNIEnv*, jobject) {
    renderGLInset(); // Inset texture coordinates
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_renderFrameBorderFixNative(JNIEnv*, jobject) {
    renderGLBorderFix(); // Clear border pixels
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_cleanupGLNative(JNIEnv*, jobject) {
    cleanupGL();
}
}