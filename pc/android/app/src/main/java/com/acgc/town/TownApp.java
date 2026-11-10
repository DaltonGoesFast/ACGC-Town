package com.acgc.town;

import android.app.Application;

/** Installs the Java crash handler before any activity loads a native library. */
public class TownApp extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        LaunchTrace.installHandler(this);
    }
}
