package jp.xenia.emulator;

import android.app.Activity;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

public abstract class WindowedAppActivity extends Activity {
    private static final String TAG = "WindowedAppActivity";

    static {
        // TODO(Triang3l): Move all demos to libxenia.so.
        System.loadLibrary("xenia-ui-window-vulkan-demo");
    }

    private long mAppContext;

    private native long initializeWindowedAppOnCreateNative(
            String windowedAppIdentifier, AssetManager assetManager);

    private native void onDestroyNative(long appContext);

    protected abstract String getWindowedAppIdentifier();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mAppContext = initializeWindowedAppOnCreateNative(getWindowedAppIdentifier(), getAssets());
        if (mAppContext == 0) {
            Log.e(TAG, "Error initializing the windowed app");
            finish();
            return;
        }
    }

    @Override
    protected void onDestroy() {
        if (mAppContext != 0) {
            onDestroyNative(mAppContext);
        }
        mAppContext = 0;
        super.onDestroy();
    }
}
