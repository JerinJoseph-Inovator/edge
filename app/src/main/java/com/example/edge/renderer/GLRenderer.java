package com.example.edge.renderer;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLRenderer extends GLSurfaceView implements GLSurfaceView.Renderer {

    private static final String TAG = "OpenGLRenderer";

    // Updated enum to include content switching (assignment requirement)
    public enum RenderModeOption {
        RAW_CAMERA,      // Show original camera feed
        EDGE_DETECTION,  // Show processed OpenCV output (your current black/white)
        GRAYSCALE,       // Optional: grayscale version
        DEFAULT,         // Keep your original for compatibility
        INSET,
        BORDER_FIX
    }

    // ORIENTATION FIX: Orientation constants
    public static final int ORIENTATION_NORMAL = 0;
    public static final int ORIENTATION_FLIPPED_V = 1;      // Most common fix for upside-down
    public static final int ORIENTATION_ROTATED_90 = 2;     // 90° clockwise
    public static final int ORIENTATION_ROTATED_180 = 3;    // 180° rotation
    public static final int ORIENTATION_ROTATED_270 = 4;    // 90° counter-clockwise

    private RenderModeOption renderMode = RenderModeOption.EDGE_DETECTION; // Start with processed
    private boolean useLinearFilter = false;
    private int currentOrientation = ORIENTATION_FLIPPED_V; // Start with most common fix

    public GLRenderer(Context context) {
        super(context);
        init();
    }

    public GLRenderer(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setEGLContextClientVersion(2);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);

        setZOrderOnTop(false);
        setZOrderMediaOverlay(true);

        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);

        Log.i(TAG, "GLRenderer initialized with orientation support");
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.i(TAG, "Surface created");
        if (useLinearFilter) {
            Log.i(TAG, "Using LINEAR filter");
            initGLLinearNative();
        } else {
            Log.i(TAG, "Using NEAREST filter");
            initGLNative();
        }

        // ORIENTATION FIX: Apply orientation after GL initialization
        setOrientationNative(currentOrientation);
        Log.i(TAG, "Applied orientation: " + getOrientationName(currentOrientation));
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.i(TAG, "Surface changed: width=" + width + ", height=" + height);
        GLES20.glViewport(0, 0, width, height);
        resizeGLNative(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        GLES20.glClearColor(0f, 0f, 0f, 1f); // Black background
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);

        // FIXED: Set the render mode BEFORE calling the render function
        setRenderModeNative(renderMode.ordinal());

        switch (renderMode) {
            case RAW_CAMERA:
                Log.d(TAG, "Rendering: RAW_CAMERA mode");
                renderFrameNative(); // Use your existing OpenGL renderer
                break;
            case EDGE_DETECTION:
                Log.d(TAG, "Rendering: EDGE_DETECTION mode");
                renderFrameNative(); // Use your existing OpenGL renderer
                break;
            case GRAYSCALE:
                Log.d(TAG, "Rendering: GRAYSCALE mode");
                renderFrameNative(); // Use your existing OpenGL renderer
                break;
            case INSET:
                Log.d(TAG, "Rendering: INSET mode");
                renderFrameInsetNative(); // Use your existing inset renderer
                break;
            case BORDER_FIX:
                Log.d(TAG, "Rendering: BORDER_FIX mode");
                renderFrameBorderFixNative(); // Use your existing border fix renderer
                break;
            case DEFAULT:
            default:
                Log.d(TAG, "Rendering: DEFAULT mode");
                renderFrameNative(); // Use your existing OpenGL renderer
                break;
        }
    }

    public void setRenderModeOption(RenderModeOption mode) {
        this.renderMode = mode;
        Log.i(TAG, "🔄 Render mode changed to: " + mode);
    }

    public RenderModeOption getCurrentRenderMode() {
        return renderMode;
    }

    public void setUseLinearFilter(boolean useLinear) {
        this.useLinearFilter = useLinear;
        Log.i(TAG, "Texture filter changed to: " + (useLinear ? "LINEAR" : "NEAREST"));
    }

    // ORIENTATION FIX: New methods to control orientation
    public void setOrientation(int orientation) {
        this.currentOrientation = orientation;
        setOrientationNative(orientation);
        Log.i(TAG, "🔄 Orientation changed to: " + getOrientationName(orientation));
    }

    public int getCurrentOrientation() {
        return currentOrientation;
    }

    // Helper method to cycle through orientations for testing
    public void cycleOrientation() {
        currentOrientation = (currentOrientation + 1) % 5;
        setOrientationNative(currentOrientation);
        Log.i(TAG, "🔄 Cycled to orientation: " + getOrientationName(currentOrientation));
    }

    // Helper method to get orientation name for logging
    private String getOrientationName(int orientation) {
        switch (orientation) {
            case ORIENTATION_NORMAL: return "NORMAL";
            case ORIENTATION_FLIPPED_V: return "FLIPPED_V";
            case ORIENTATION_ROTATED_90: return "ROTATED_90";
            case ORIENTATION_ROTATED_180: return "ROTATED_180";
            case ORIENTATION_ROTATED_270: return "ROTATED_270";
            default: return "UNKNOWN";
        }
    }

    // Quick fix methods for common issues
    public void fixUpsideDown() {
        setOrientation(ORIENTATION_FLIPPED_V);
        Log.i(TAG, "🔧 Applied upside-down fix");
    }

    public void rotate180() {
        setOrientation(ORIENTATION_ROTATED_180);
        Log.i(TAG, "🔧 Applied 180° rotation");
    }

    public void rotate90Clockwise() {
        setOrientation(ORIENTATION_ROTATED_90);
        Log.i(TAG, "🔧 Applied 90° clockwise rotation");
    }

    public void rotate90CounterClockwise() {
        setOrientation(ORIENTATION_ROTATED_270);
        Log.i(TAG, "🔧 Applied 90° counter-clockwise rotation");
    }

    public void resetOrientation() {
        setOrientation(ORIENTATION_NORMAL);
        Log.i(TAG, "🔧 Reset to normal orientation");
    }

    public void cleanup() {
        Log.i(TAG, "Cleaning up GL resources");
        cleanupGLNative();
    }

    // JNI bindings - FIXED: Using your actual function names from opengl_renderer.cpp
    private native void initGLNative();
    private native void initGLLinearNative();
    private native void resizeGLNative(int width, int height);
    private native void renderFrameNative();
    private native void renderFrameInsetNative();
    private native void renderFrameBorderFixNative();
    private native void cleanupGLNative();
    private native void setRenderModeNative(int mode); // Tell native which content to render
    private native void setOrientationNative(int orientation); // NEW: Set orientation

    static {
        try {
            System.loadLibrary("edge");
            Log.i(TAG, "Native library 'edge' loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library: " + e.getMessage());
        }
    }
}