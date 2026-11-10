package com.acgc.town;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

import java.io.File;

/** SDL2 activity. The game binary is not linked yet; libmain opens a GLES 3 context. */
public class TownActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getArguments() {
        File rom = new File(getFilesDir(), "gafe01.iso");
        if (!rom.isFile()) return new String[] { "--no-rom" };
        return new String[] { "--rom", rom.getAbsolutePath() };
    }
}
