package com.acgc.town;

import android.app.Activity;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;

/** Writes cutout and system-bar insets so the touch HUD can stay off the Fold's hinge, camera hole, and cover-screen edges. */
public final class SafeInsets {
    private SafeInsets() {}

    public static void install(Activity activity) {
        WindowManager.LayoutParams lp = activity.getWindow().getAttributes();
        if (Build.VERSION.SDK_INT >= 28) {
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            activity.getWindow().setAttributes(lp);
        }
        View decor = activity.getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((v, insets) -> {
            write(activity, insets);
            return insets;
        });
        decor.post(() -> write(activity, decor.getRootWindowInsets()));
    }

    private static void write(Activity activity, WindowInsets insets) {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (insets != null) {
            left = insets.getSystemWindowInsetLeft();
            top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight();
            bottom = insets.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT >= 28) {
                DisplayCutout cut = insets.getDisplayCutout();
                if (cut != null) {
                    left = Math.max(left, cut.getSafeInsetLeft());
                    top = Math.max(top, cut.getSafeInsetTop());
                    right = Math.max(right, cut.getSafeInsetRight());
                    bottom = Math.max(bottom, cut.getSafeInsetBottom());
                }
            }
        }
        File out = new File(activity.getFilesDir(), "safe_insets.txt");
        try (FileOutputStream fos = new FileOutputStream(out)) {
            fos.write((left + " " + top + " " + right + " " + bottom + "\n").getBytes());
        } catch (Exception ignored) {
        }
    }
}
