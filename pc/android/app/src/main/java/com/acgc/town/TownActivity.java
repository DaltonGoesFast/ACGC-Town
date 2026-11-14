package com.acgc.town;

import android.content.pm.ActivityInfo;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/** SDL2 activity. libmain.so is the game. Shaders are copied next to the saved disc before SDL starts. */
public class TownActivity extends SDLActivity {
    private void copyAsset(String name, File dest) {
        dest.getParentFile().mkdirs();
        try (InputStream in = getAssets().open(name);
             FileOutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (Exception ignored) {
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        LaunchTrace.mark(this);
        File shaders = new File(getFilesDir(), "shaders");
        copyAsset("default.vert", new File(shaders, "default.vert"));
        copyAsset("default.frag", new File(shaders, "default.frag"));
        Log.i("ACGC", "TownActivity start, rom=" + new File(getFilesDir(), "gafe01.iso").getAbsolutePath());
        super.onCreate(savedInstanceState);
        SafeInsets.install(this);
        View decor = getWindow().getDecorView();
        decor.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            applyImmersive();
            applyGestureExclusion();
        });
        applyImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersive();
            applyGestureExclusion();
        }
    }

    /** Sticky immersive fullscreen so the status bar and gesture bar do not sit in the touch stream. */
    private void applyImmersive() {
        View decor = getWindow().getDecorView();
        int flags = View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        decor.setSystemUiVisibility(flags);
    }

    /** Keep back-swipe and the notification shade off the HUD corners. */
    private void applyGestureExclusion() {
        if (Build.VERSION.SDK_INT < 29) return;
        View decor = getWindow().getDecorView();
        int w = decor.getWidth();
        int h = decor.getHeight();
        if (w < 2 || h < 2) return;
        float d = getResources().getDisplayMetrics().density;
        int edge = Math.max(1, (int) (48f * d));
        int corner = Math.max(edge, (int) (168f * d));
        if (corner > h) corner = h;
        List<Rect> rects = new ArrayList<>();
        rects.add(new Rect(0, 0, Math.min(edge, w), corner));
        rects.add(new Rect(0, h - corner, Math.min(edge, w), h));
        rects.add(new Rect(Math.max(0, w - edge), 0, w, corner));
        rects.add(new Rect(Math.max(0, w - edge), h - corner, w, h));
        decor.setSystemGestureExclusionRects(rects);
    }

    @Override
    public void loadLibraries() {
        LaunchTrace.mark(this);
        String main = new File(getApplicationInfo().nativeLibraryDir, "libmain.so").getAbsolutePath();
        Log.i("ACGC", "placing " + main + " below 4GB");
        try {
            String err = LowLoad.place(main);
            if (err != null) {
                LaunchTrace.note(this, err);
                throw new UnsatisfiedLinkError(err);
            }
            /* place() already mapped these. System.loadLibrary is what registers
             * JNI_OnLoad with this class loader. The linker returns the existing
             * mapping, so libmain stays at the low address. */
            System.loadLibrary("SDL2");
            System.loadLibrary("main");
            Log.i("ACGC", "native libraries loaded below 4GB");
        } catch (UnsatisfiedLinkError e) {
            LaunchTrace.note(this, "UnsatisfiedLinkError\n" + Log.getStackTraceString(e));
            throw e;
        } catch (RuntimeException e) {
            LaunchTrace.note(this, "loadLibrary failed\n" + Log.getStackTraceString(e));
            throw e;
        }
    }

    /** SDL asks for an orientation every time the surface comes back. If we are already free
     * to rotate, asking again pauses the activity and the game thread blocks for seconds. */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        int cur = getRequestedOrientation();
        if (cur == ActivityInfo.SCREEN_ORIENTATION_FULL_USER
                || cur == ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR
                || cur == ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED) {
            Log.i("ACGC", "skip orientation request, already " + cur);
            return;
        }
        super.setOrientationBis(w, h, resizable, hint);
    }

    @Override
    protected void onPause() {
        LaunchTrace.clear(this);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (isFinishing()) LaunchTrace.clear(this);
        super.onDestroy();
    }

    @Override
    protected String[] getArguments() {
        File rom = new File(getFilesDir(), "gafe01.iso");
        String romPath = rom.isFile() ? rom.getAbsolutePath() : "";
        android.content.Intent intent = getIntent();
        if (intent != null && intent.hasExtra("args")) {
            String[] extra = intent.getStringArrayExtra("args");
            if (extra != null && extra.length > 0) {
                return extra;
            }
        }
        if (!rom.isFile()) return new String[] { "--no-rom" };
        return new String[] { "--rom", romPath };
    }
}
