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

    public enum RenderModeOption {
        DEFAULT,
        INSET,
        BORDER_FIX
    }

    private RenderModeOption renderMode = RenderModeOption.DEFAULT;
    private boolean useLinearFilter = false;

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

        Log.i(TAG, "GLRenderer initialized");
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
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.i(TAG, "Surface changed: width=" + width + ", height=" + height);
        GLES20.glViewport(0, 0, width, height);
        resizeGLNative(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        GLES20.glClearColor(1f, 0f, 0f, 1f);
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);

        switch (renderMode) {
            case INSET:
                Log.i(TAG, "Rendering: INSET mode");
                renderFrameInsetNative();
                break;
            case BORDER_FIX:
                Log.i(TAG, "Rendering: BORDER_FIX mode");
                renderFrameBorderFixNative();
                break;
            case DEFAULT:
            default:
                Log.i(TAG, "Rendering: DEFAULT mode");
                renderFrameNative();
                break;
        }
    }

    public void setRenderModeOption(RenderModeOption mode) {
        this.renderMode = mode;
        Log.i(TAG, "Render mode changed to: " + mode);
    }

    public void setUseLinearFilter(boolean useLinear) {
        this.useLinearFilter = useLinear;
        Log.i(TAG, "Texture filter changed to: " + (useLinear ? "LINEAR" : "NEAREST"));
    }

    public void cleanup() {
        Log.i(TAG, "Cleaning up GL resources");
        cleanupGLNative();
    }

    // JNI bindings
    private native void initGLNative();
    private native void initGLLinearNative();
    private native void resizeGLNative(int width, int height);
    private native void renderFrameNative();
    private native void renderFrameInsetNative();
    private native void renderFrameBorderFixNative();
    private native void cleanupGLNative();

    static {
        try {
            System.loadLibrary("edge");
            Log.i(TAG, "Native library 'edge' loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library: " + e.getMessage());
        }
    }
}
