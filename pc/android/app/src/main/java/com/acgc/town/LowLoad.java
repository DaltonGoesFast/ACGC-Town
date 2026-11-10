package com.acgc.town;

/** Loads libmain.so into a hole below 4 GB so static game buffers fit in a u32. */
public final class LowLoad {
    static {
        System.loadLibrary("lowload");
    }

    private LowLoad() {}

    /** @return null on success, otherwise an error string */
    public static native String place(String path);
}
