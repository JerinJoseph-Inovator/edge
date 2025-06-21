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

// ORIENTATION FIX: Different vertex arrays for different orientations
// Normal orientation (portrait, no rotation needed)
static const GLfloat verticesNormal[] = {
        // Position     // Texture coords
        -1.0f, -1.0f,   0.0f, 0.0f,    // Bottom-left
        1.0f, -1.0f,   1.0f, 0.0f,    // Bottom-right
        -1.0f,  1.0f,   0.0f, 1.0f,    // Top-left
        1.0f,  1.0f,   1.0f, 1.0f     // Top-right
};

// Flipped vertically (fixes upside-down camera)
static const GLfloat verticesFlippedV[] = {
        // Position     // Texture coords (V flipped)
        -1.0f, -1.0f,   0.0f, 1.0f,    // Bottom-left -> Top-left of texture
        1.0f, -1.0f,   1.0f, 1.0f,    // Bottom-right -> Top-right of texture
        -1.0f,  1.0f,   0.0f, 0.0f,    // Top-left -> Bottom-left of texture
        1.0f,  1.0f,   1.0f, 0.0f     // Top-right -> Bottom-right of texture
};

// Rotated 90 degrees clockwise
static const GLfloat verticesRotated90[] = {
        // Position     // Texture coords (90° CW rotation)
        -1.0f, -1.0f,   1.0f, 0.0f,    // Bottom-left -> Bottom-right of texture
        1.0f, -1.0f,   1.0f, 1.0f,    // Bottom-right -> Top-right of texture
        -1.0f,  1.0f,   0.0f, 0.0f,    // Top-left -> Bottom-left of texture
        1.0f,  1.0f,   0.0f, 1.0f     // Top-right -> Top-left of texture
};

// Rotated 180 degrees
static const GLfloat verticesRotated180[] = {
        // Position     // Texture coords (180° rotation)
        -1.0f, -1.0f,   1.0f, 1.0f,    // Bottom-left -> Top-right of texture
        1.0f, -1.0f,   0.0f, 1.0f,    // Bottom-right -> Top-left of texture
        -1.0f,  1.0f,   1.0f, 0.0f,    // Top-left -> Bottom-right of texture
        1.0f,  1.0f,   0.0f, 0.0f     // Top-right -> Bottom-left of texture
};

// Rotated 270 degrees clockwise (or 90 CCW)
static const GLfloat verticesRotated270[] = {
        // Position     // Texture coords (270° CW rotation)
        -1.0f, -1.0f,   0.0f, 1.0f,    // Bottom-left -> Top-left of texture
        1.0f, -1.0f,   0.0f, 0.0f,    // Bottom-right -> Bottom-left of texture
        -1.0f,  1.0f,   1.0f, 1.0f,    // Top-left -> Top-right of texture
        1.0f,  1.0f,   1.0f, 0.0f     // Top-right -> Bottom-right of texture
};

// Current orientation setting
enum class Orientation {
    NORMAL = 0,
    FLIPPED_V = 1,
    ROTATED_90 = 2,
    ROTATED_180 = 3,
    ROTATED_270 = 4
};

static Orientation currentOrientation = Orientation::FLIPPED_V; // Start with flipped (most common fix)

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

// Helper function to get the right vertices for current orientation
static const GLfloat* getCurrentVertices() {
    switch (currentOrientation) {
        case Orientation::NORMAL: return verticesNormal;
        case Orientation::FLIPPED_V: return verticesFlippedV;
        case Orientation::ROTATED_90: return verticesRotated90;
        case Orientation::ROTATED_180: return verticesRotated180;
        case Orientation::ROTATED_270: return verticesRotated270;
        default: return verticesFlippedV;
    }
}

void initGL() {
    glDisable(GL_DITHER);
    checkGLError("disable dither");

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

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

    LOGI("initGL complete with orientation support");
}

void resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    checkGLError("glViewport");
}

// Main render function with orientation support
void renderGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    cv::Mat frame;
    try {
        frame = getLatestFrameForRender();
        if (frame.empty()) {
            return;
        }
    } catch (const std::exception& e) {
        LOGE("Exception getting frame: %s", e.what());
        return;
    }

    if (frame.data == nullptr || frame.cols <= 0 || frame.rows <= 0) {
        LOGE("Invalid frame data");
        return;
    }

    // Convert to RGBA
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
        LOGE("OpenCV color conversion failed: %s", e.what());
        return;
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

    // Render with correct orientation
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(samplerLoc, 0);

    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(texLoc);

    // Use vertices based on current orientation
    const GLfloat* vertices = getCurrentVertices();
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGLError("draw arrays");

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(texLoc);
}

// Function to set orientation from Java
void setOrientation(int orientation) {
    currentOrientation = static_cast<Orientation>(orientation);
    LOGI("Orientation set to: %d", orientation);
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

// JNI exports
#include <jni.h>
extern "C" {

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_initGLNative(JNIEnv*, jobject) {
    initGL();
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_resizeGLNative(JNIEnv*, jobject, jint w, jint h) {
    resizeGL(w, h);
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_renderFrameNative(JNIEnv*, jobject) {
    renderGL();
}

// NEW: Set orientation from Java
JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_setOrientationNative(JNIEnv*, jobject, jint orientation) {
    setOrientation(orientation);
}

JNIEXPORT void JNICALL Java_com_example_edge_renderer_GLRenderer_cleanupGLNative(JNIEnv*, jobject) {
    cleanupGL();
}

}