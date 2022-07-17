package jp.xenia.emulator;

import android.os.Bundle;

public class GpuTraceViewerActivity extends WindowedAppActivity {
    @Override
    protected String getWindowedAppIdentifier() {
        return "xenia_gpu_vulkan_trace_viewer";
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_gpu_trace_viewer);
        setWindowSurfaceView(findViewById(R.id.gpu_trace_viewer_surface_view));
    }
}
