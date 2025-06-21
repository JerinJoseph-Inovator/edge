package com.example.edge.debug;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;
import android.util.Log;

public class DebugOverlayView extends View {

    private static final String TAG = "DebugOverlay";

    private final Paint paint;
    private String modeText = "Mode: DEFAULT";
    private String filterText = "Filter: NEAREST";
    private String fpsText = "FPS: ...";

    private int frameCount = 0;
    private long lastTime = System.nanoTime();

    public DebugOverlayView(Context context) {
        this(context, null);
    }

    public DebugOverlayView(Context context, AttributeSet attrs) {
        super(context, attrs);
        paint = new Paint();
        paint.setColor(Color.WHITE);
        paint.setTextSize(36f);
        paint.setAntiAlias(true);
        paint.setShadowLayer(3, 2, 2, Color.BLACK);
    }

    public void updateRenderInfo(String mode, String filter) {
        this.modeText = "Mode: " + (mode != null ? mode : "DEFAULT");
        this.filterText = "Filter: " + (filter != null ? filter : "NEAREST");
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        frameCount++;
        long now = System.nanoTime();
        long elapsed = now - lastTime;

        if (elapsed >= 1_000_000_000L) { // 1 second in nanoseconds
            int fps = (int) ((frameCount * 1_000_000_000L) / elapsed);
            fpsText = "FPS: " + fps;
            Log.d(TAG, fpsText); // Optional: Log FPS
            frameCount = 0;
            lastTime = now;
        }

        float y = 60f;
        float lineHeight = 50f;

        canvas.drawText(fpsText, 20, y, paint);
        canvas.drawText(modeText, 20, y + lineHeight, paint);
        canvas.drawText(filterText, 20, y + lineHeight * 2, paint);

        postInvalidateDelayed(16); // redraw ~60fps
    }
}
