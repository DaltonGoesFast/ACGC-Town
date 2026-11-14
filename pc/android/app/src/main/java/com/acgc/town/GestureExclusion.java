package com.acgc.town;

import android.app.Activity;
import android.graphics.Rect;
import android.os.Build;
import android.util.Log;
import android.view.View;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.List;

/**
 * Reads gesture_exclusion.txt (written by native HUD layout) and applies
 * setSystemGestureExclusionRects so corner Bag/Map/Action/Back/Menu discs
 * are less likely to trigger system back / gesture arcs (Fold, phones).
 */
public final class GestureExclusion {
    private static final long POLL_MS = 250L;
    private static Runnable sPoll;

    private GestureExclusion() {}

    public static void install(Activity activity) {
        if (Build.VERSION.SDK_INT < 29) return;
        View decor = activity.getWindow().getDecorView();
        sPoll = new Runnable() {
            @Override
            public void run() {
                apply(activity, decor);
                decor.postDelayed(this, POLL_MS);
            }
        };
        decor.post(sPoll);
    }

    private static void apply(Activity activity, View decor) {
        File f = new File(activity.getFilesDir(), "gesture_exclusion.txt");
        if (!f.isFile()) return;
        List<Rect> rects = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new FileReader(f))) {
            String line = br.readLine();
            if (line == null) return;
            int n = Integer.parseInt(line.trim());
            for (int i = 0; i < n; i++) {
                line = br.readLine();
                if (line == null) break;
                String[] p = line.trim().split("\\s+");
                if (p.length < 4) continue;
                int l = Math.round(Float.parseFloat(p[0]));
                int t = Math.round(Float.parseFloat(p[1]));
                int r = Math.round(Float.parseFloat(p[2]));
                int b = Math.round(Float.parseFloat(p[3]));
                if (r > l && b > t) rects.add(new Rect(l, t, r, b));
            }
        } catch (Exception e) {
            return;
        }
        try {
            decor.setSystemGestureExclusionRects(rects);
        } catch (Exception e) {
            Log.w("ACGC", "gesture exclusion failed: " + e);
        }
    }
}
