package com.example.edge.camera;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.*;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;
import android.view.Display;
import android.view.Surface;
import android.view.TextureView;
import android.view.WindowManager;

import java.nio.ByteBuffer;
import java.util.Arrays;

public class CameraController {

    private static final String TAG = "CameraController";

    private final Context context;
    private final TextureView textureView;
    private final FrameCallback callback;

    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private Handler backgroundHandler;
    private ImageReader imageReader;

    private Size selectedSize;
    private int sensorOrientation;
    private int displayRotation;

    public interface FrameCallback {
        void onFrameAvailable(byte[] frameData, int width, int height);
    }

    public CameraController(Context context, TextureView textureView, FrameCallback callback) {
        this.context = context;
        this.textureView = textureView;
        this.callback = callback;
    }

    public void startCamera() {
        if (textureView.isAvailable()) {
            Log.d(TAG, "✅ TextureView available — initializing camera...");
            initializeCamera();
        } else {
            Log.d(TAG, "⏳ Waiting for TextureView to become available...");
            textureView.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
                @Override
                public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
                    Log.d(TAG, "✅ SurfaceTexture is now available");
                    initializeCamera();
                }

                @Override public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
                    configureTransform(width, height);
                }
                @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) { return false; }
                @Override public void onSurfaceTextureUpdated(SurfaceTexture surface) {}
            });
        }
    }

    private void initializeCamera() {
        try {
            Log.d(TAG, "📸 Initializing camera backend...");
            HandlerThread backgroundThread = new HandlerThread("CameraBackground");
            backgroundThread.start();
            backgroundHandler = new Handler(backgroundThread.getLooper());

            CameraManager manager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
            String cameraId = manager.getCameraIdList()[0];

            CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);

            // Get sensor orientation
            sensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
            Log.i(TAG, "📐 Camera sensor orientation: " + sensorOrientation);

            // Get display rotation
            WindowManager windowManager = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            Display display = windowManager.getDefaultDisplay();
            displayRotation = display.getRotation();
            Log.i(TAG, "📱 Display rotation: " + displayRotation);

            Size[] sizes = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                    .getOutputSizes(ImageFormat.YUV_420_888);

            for (Size s : sizes) {
                if (s.getWidth() <= 640 && s.getHeight() <= 480) {
                    selectedSize = s;
                    break;
                }
            }
            if (selectedSize == null) {
                selectedSize = sizes[sizes.length - 1];
            }

            Log.i(TAG, "📐 Selected camera preview size: " +
                    selectedSize.getWidth() + "x" + selectedSize.getHeight());

            imageReader = ImageReader.newInstance(
                    selectedSize.getWidth(),
                    selectedSize.getHeight(),
                    ImageFormat.YUV_420_888,
                    2
            );

            imageReader.setOnImageAvailableListener(reader -> {
                Image image = reader.acquireLatestImage();
                if (image == null) {
                    Log.w(TAG, "⚠️ acquireLatestImage returned null");
                    return;
                }

                try {
                    Log.d(TAG, "📷 Frame received: " + image.getWidth() + "x" + image.getHeight());

                    // Convert YUV420 to NV21
                    byte[] nv21 = convertYUV420888ToNV21(image);

                    Log.d(TAG, "✅ JNI processFrame called with size: " + image.getWidth() + "x" + image.getHeight());

                    // Calculate rotation needed
                    int rotation = getRotationCompensation();
                    Log.d(TAG, "🔄 Applying rotation compensation: " + rotation + "°");

                    // Send to native processing with rotation info
                    com.example.edge.nativebridge.NativeBridge.processFrameWithRotation(nv21, image.getWidth(), image.getHeight(), rotation);

                    // Callback to UI if needed
                    if (callback != null) {
                        callback.onFrameAvailable(nv21, image.getWidth(), image.getHeight());
                    }
                } catch (Exception e) {
                    Log.e(TAG, "❌ Error in frame processing: " + e.getMessage());
                    e.printStackTrace();
                } finally {
                    image.close();
                }
            }, backgroundHandler);

            if (context.checkSelfPermission(android.Manifest.permission.CAMERA)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                Log.e(TAG, "🚫 Camera permission not granted");
                return;
            }

            manager.openCamera(cameraId, new CameraDevice.StateCallback() {
                @Override
                public void onOpened(CameraDevice device) {
                    Log.i(TAG, "✅ Camera opened");
                    cameraDevice = device;
                    createCameraPreviewSession();
                }

                @Override
                public void onDisconnected(CameraDevice device) {
                    Log.w(TAG, "⚠️ Camera disconnected");
                    device.close();
                }

                @Override
                public void onError(CameraDevice device, int error) {
                    Log.e(TAG, "❌ Camera error: " + error);
                    device.close();
                }
            }, backgroundHandler);

        } catch (Exception e) {
            Log.e(TAG, "❌ Exception initializing camera: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private void createCameraPreviewSession() {
        try {
            SurfaceTexture texture = textureView.getSurfaceTexture();
            if (texture == null) {
                Log.e(TAG, "❌ SurfaceTexture is null");
                return;
            }

            // Configure the size of default buffer to be the size of camera preview we want
            texture.setDefaultBufferSize(selectedSize.getWidth(), selectedSize.getHeight());

            // Configure transform matrix for proper orientation
            configureTransform(textureView.getWidth(), textureView.getHeight());

            Surface previewSurface = new Surface(texture);
            Surface processingSurface = imageReader.getSurface();

            CaptureRequest.Builder builder = cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            builder.addTarget(previewSurface);
            builder.addTarget(processingSurface);

            cameraDevice.createCaptureSession(Arrays.asList(previewSurface, processingSurface),
                    new CameraCaptureSession.StateCallback() {
                        @Override
                        public void onConfigured(CameraCaptureSession session) {
                            captureSession = session;
                            try {
                                session.setRepeatingRequest(builder.build(), null, backgroundHandler);
                                Log.i(TAG, "🔁 Repeating request started");
                            } catch (CameraAccessException e) {
                                Log.e(TAG, "❌ Failed to start repeating request: " + e.getMessage());
                            }
                        }

                        @Override
                        public void onConfigureFailed(CameraCaptureSession session) {
                            Log.e(TAG, "❌ Failed to configure camera capture session");
                        }
                    }, backgroundHandler);
        } catch (CameraAccessException e) {
            Log.e(TAG, "❌ CameraAccessException during preview setup: " + e.getMessage());
        }
    }

    /**
     * Configure the transform matrix for the TextureView to account for orientation
     */
    private void configureTransform(int viewWidth, int viewHeight) {
        if (null == textureView || null == selectedSize) {
            return;
        }

        int rotation = getRotationCompensation();
        android.graphics.Matrix matrix = new android.graphics.Matrix();
        android.graphics.RectF viewRect = new android.graphics.RectF(0, 0, viewWidth, viewHeight);
        android.graphics.RectF bufferRect = new android.graphics.RectF(0, 0, selectedSize.getHeight(), selectedSize.getWidth());
        float centerX = viewRect.centerX();
        float centerY = viewRect.centerY();

        if (Surface.ROTATION_90 == displayRotation || Surface.ROTATION_270 == displayRotation) {
            bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY());
            matrix.setRectToRect(viewRect, bufferRect, android.graphics.Matrix.ScaleToFit.FILL);
            float scale = Math.max(
                    (float) viewHeight / selectedSize.getHeight(),
                    (float) viewWidth / selectedSize.getWidth());
            matrix.postScale(scale, scale, centerX, centerY);
            matrix.postRotate(90 * (displayRotation - 2), centerX, centerY);
        } else if (Surface.ROTATION_180 == displayRotation) {
            matrix.postRotate(180, centerX, centerY);
        }

        textureView.setTransform(matrix);
        Log.i(TAG, "🔄 Transform matrix configured for rotation: " + rotation + "°");
    }

    /**
     * Calculate the rotation compensation needed
     */
    private int getRotationCompensation() {
        // Get the device's current rotation relative to its "native" orientation.
        int deviceRotation = displayRotation;
        int rotationCompensation = 0;

        // Calculate the camera rotation relative to the device's "native" orientation.
        switch (deviceRotation) {
            case Surface.ROTATION_0:
                rotationCompensation = sensorOrientation;
                break;
            case Surface.ROTATION_90:
                rotationCompensation = (sensorOrientation - 90 + 360) % 360;
                break;
            case Surface.ROTATION_180:
                rotationCompensation = (sensorOrientation - 180 + 360) % 360;
                break;
            case Surface.ROTATION_270:
                rotationCompensation = (sensorOrientation - 270 + 360) % 360;
                break;
        }

        return rotationCompensation;
    }

    public void stopCamera() {
        Log.d(TAG, "🛑 Stopping camera...");
        if (captureSession != null) captureSession.close();
        if (cameraDevice != null) cameraDevice.close();
        if (imageReader != null) imageReader.close();
    }

    /**
     * Fixed YUV420_888 to NV21 conversion
     * This handles different pixel stride scenarios properly
     */
    private byte[] convertYUV420888ToNV21(Image image) {
        Image.Plane[] planes = image.getPlanes();
        Image.Plane yPlane = planes[0];
        Image.Plane uPlane = planes[1];
        Image.Plane vPlane = planes[2];

        ByteBuffer yBuffer = yPlane.getBuffer();
        ByteBuffer uBuffer = uPlane.getBuffer();
        ByteBuffer vBuffer = vPlane.getBuffer();

        int ySize = yBuffer.remaining();
        int uSize = uBuffer.remaining();
        int vSize = vBuffer.remaining();

        byte[] nv21 = new byte[ySize + uSize + vSize];

        // Copy Y plane directly
        yBuffer.get(nv21, 0, ySize);

        // Handle UV planes - NV21 format is YYYYYYYY VUVUVUVU
        int uvPixelStride = uPlane.getPixelStride();

        if (uvPixelStride == 1) {
            // Packed case - pixels are adjacent
            uBuffer.get(nv21, ySize, uSize);
            vBuffer.get(nv21, ySize + uSize, vSize);

            // Rearrange to NV21 format (VUVUVUVU)
            for (int i = 0; i < uSize; i++) {
                nv21[ySize + i * 2] = nv21[ySize + uSize + i]; // V
                nv21[ySize + i * 2 + 1] = nv21[ySize + i];     // U
            }
        } else {
            // Semi-planar or other format - handle pixel stride
            int width = image.getWidth();
            int height = image.getHeight();
            int uvIndex = ySize;

            for (int row = 0; row < height / 2; row++) {
                for (int col = 0; col < width / 2; col++) {
                    int vuPos = row * vPlane.getRowStride() + col * uvPixelStride;
                    int uPos = row * uPlane.getRowStride() + col * uvPixelStride;

                    if (vuPos < vSize && uPos < uSize) {
                        nv21[uvIndex++] = vBuffer.get(vuPos); // V
                        nv21[uvIndex++] = uBuffer.get(uPos);  // U
                    }
                }
            }
        }

        return nv21;
    }
}