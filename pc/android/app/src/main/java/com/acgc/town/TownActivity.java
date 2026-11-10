package com.acgc.town;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

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
        File shaders = new File(getFilesDir(), "shaders");
        copyAsset("default.vert", new File(shaders, "default.vert"));
        copyAsset("default.frag", new File(shaders, "default.frag"));
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getArguments() {
        File rom = new File(getFilesDir(), "gafe01.iso");
        if (!rom.isFile()) return new String[] { "--no-rom" };
        return new String[] { "--rom", rom.getAbsolutePath() };
    }
}
