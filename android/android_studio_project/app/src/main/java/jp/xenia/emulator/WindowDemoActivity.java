package jp.xenia.emulator;

import android.os.Bundle;

public class WindowDemoActivity extends WindowedAppActivity {
    @Override
    protected String getWindowedAppIdentifier() {
        return "xenia_ui_window_vulkan_demo";
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_window_demo);
        setWindowSurfaceView(findViewById(R.id.window_demo_surface_view));
    }
}
