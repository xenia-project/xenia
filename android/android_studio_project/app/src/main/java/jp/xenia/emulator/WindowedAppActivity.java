package jp.xenia.emulator;

import android.app.Activity;
import android.content.res.AssetManager;
import android.os.Bundle;

import jp.xenia.XeniaRuntimeException;

public abstract class WindowedAppActivity extends Activity {
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

        final String windowedAppIdentifier = getWindowedAppIdentifier();
        mAppContext = initializeWindowedAppOnCreateNative(windowedAppIdentifier, getAssets());
        if (mAppContext == 0) {
            finish();
            throw new XeniaRuntimeException(
                    "Error initializing the windowed app " + windowedAppIdentifier);
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
