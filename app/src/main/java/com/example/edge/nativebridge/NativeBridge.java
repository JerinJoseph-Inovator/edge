package com.example.edge.nativebridge;

import android.util.Log;

public class NativeBridge {

    private static final String TAG = "NativeBridge";

    static {
        try {
            System.loadLibrary("edge");
            Log.i(TAG, "Native library 'edge' loaded successfully");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library: " + e.getMessage());
        }
    }

    // Pushes a processed frame into native buffer (for OpenGL to render)
    public static void processFrame(byte[] frameData, int width, int height) {
        Log.d(TAG, "Calling native processFrame()");
        nativeProcessFrame(frameData, width, height);
    }

    // Optional: Cleanup native memory (called from onDestroy or cleanup hooks)
    public static void cleanup() {
        Log.d(TAG, "Calling native cleanup()");
        nativeCleanup();
    }

    // Native methods (hidden behind wrapper to allow logging)
    private static native void nativeProcessFrame(byte[] frameData, int width, int height);
    private static native void nativeCleanup();
}
