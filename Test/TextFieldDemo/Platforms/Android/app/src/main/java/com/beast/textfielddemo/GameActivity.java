package com.beast.textfielddemo;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.graphics.Insets;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

// Android owns system UI; TextFieldDemo owns layout, rendering and input coordinates.
public class GameActivity extends NativeActivity {
    // UI-thread snapshot: four unsigned 16-bit physical-pixel margins.
    // -1 means that Android has not delivered insets yet.
    private volatile long beastSafeInsets = -1;
    public long getTextFieldDemoSafeInsets() { return beastSafeInsets; }

    @Override
    @SuppressWarnings("deprecation")
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(0xffffffff);
        getWindow().setNavigationBarColor(0xffffffff);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(attributes);
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            int left, top, right, bottom;
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsetsIgnoringVisibility(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                left = safe.left; top = safe.top; right = safe.right; bottom = safe.bottom;
            } else {
                left = insets.getStableInsetLeft(); top = insets.getStableInsetTop();
                right = insets.getStableInsetRight(); bottom = insets.getStableInsetBottom();
                DisplayCutout cutout = insets.getDisplayCutout();
                if (cutout != null) {
                    left = Math.max(left, cutout.getSafeInsetLeft());
                    top = Math.max(top, cutout.getSafeInsetTop());
                    right = Math.max(right, cutout.getSafeInsetRight());
                    bottom = Math.max(bottom, cutout.getSafeInsetBottom());
                }
            }
            beastSafeInsets = pack(left) | (pack(top) << 16) |
                (pack(right) << 32) | (pack(bottom) << 48);
            return view.onApplyWindowInsets(insets);
        });
        decor.requestApplyInsets();
    }

    private static long pack(int value) { return Math.max(0, Math.min(65535, value)); }
}
