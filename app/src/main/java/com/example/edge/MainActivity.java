package com.example.edge;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.view.TextureView;
import android.view.View;
import android.widget.AdapterView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import com.example.edge.camera.CameraController;
import com.example.edge.databinding.ActivityMainBinding;
import com.example.edge.debug.DebugOverlayView;
import com.example.edge.renderer.GLRenderer;
import com.example.edge.renderer.GLRenderer.RenderModeOption;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "MainActivity";
    private static final int CAMERA_PERMISSION_REQUEST = 100;

    private ActivityMainBinding binding;
    private TextureView previewTexture;
    private GLRenderer glRenderer;
    private DebugOverlayView debugOverlay;
    private CameraController cameraController;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // ❌ REMOVED: Don't load library here - NativeBridge already loads it
        // The library should only be loaded once to avoid crashes

        Log.i(TAG, "🚀 MainActivity onCreate - native library loaded by NativeBridge");

        previewTexture = binding.previewTexture;
        previewTexture.setVisibility(View.VISIBLE); // Ensure it's visible for camera

        // ✅ Overlay debug info on top of GLRenderer
        debugOverlay = new DebugOverlayView(this);
        binding.glContainer.addView(debugOverlay);

        setupUIControls();
        setupGLRenderer(false, RenderModeOption.DEFAULT);

        requestCameraPermission();
    }

    private void setupGLRenderer(boolean useLinear, RenderModeOption mode) {
        if (glRenderer != null) {
            glRenderer.cleanup();
            binding.glContainer.removeView(glRenderer);
        }

        glRenderer = new GLRenderer(this);
        glRenderer.setUseLinearFilter(useLinear);
        glRenderer.setRenderModeOption(mode);
        glRenderer.setZOrderOnTop(false);
        binding.glContainer.addView(glRenderer, 0); // add below overlay

        if (debugOverlay != null) {
            debugOverlay.updateRenderInfo(mode.name(), useLinear ? "LINEAR" : "NEAREST");
        }

        Log.i(TAG, "🎨 GLRenderer configured → Mode: " + mode + ", Filter: " + (useLinear ? "LINEAR" : "NEAREST"));
    }

    private void setupUIControls() {
        Spinner renderModeSpinner = binding.renderModeSpinner;
        Switch filterSwitch = binding.filterSwitch;

        renderModeSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                RenderModeOption mode = RenderModeOption.values()[position];
                boolean useLinear = filterSwitch.isChecked();
                setupGLRenderer(useLinear, mode);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });

        filterSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            RenderModeOption mode = RenderModeOption.values()[renderModeSpinner.getSelectedItemPosition()];
            setupGLRenderer(isChecked, mode);
        });
    }

    private void requestCameraPermission() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_REQUEST);
        } else {
            startCameraPreview();
        }
    }

    private void startCameraPreview() {
        Log.i(TAG, "📷 Initializing camera preview...");

        cameraController = new CameraController(this, previewTexture, (frameData, width, height) -> {
            Log.d(TAG, "📤 Frame delivered from CameraController: " + width + "x" + height);
            // Don't call NativeBridge here — it's already called inside CameraController
        });

        previewTexture.post(() -> {
            Log.d(TAG, "📺 TextureView ready — starting camera");
            cameraController.startCamera();
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        if (requestCode == CAMERA_PERMISSION_REQUEST &&
                grantResults.length > 0 &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startCameraPreview();
        } else {
            Toast.makeText(this, "❌ Camera permission is required to run", Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (cameraController != null) {
            Log.i(TAG, "⏸️ Pausing camera");
            cameraController.stopCamera();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (cameraController != null) {
            cameraController.stopCamera();
        }
        if (glRenderer != null) {
            glRenderer.cleanup();
        }

        // Clean up native resources
        try {
            com.example.edge.nativebridge.NativeBridge.cleanup();
            Log.i(TAG, "✅ Native cleanup completed");
        } catch (Exception e) {
            Log.e(TAG, "❌ Error during native cleanup: " + e.getMessage());
        }
    }
}