package com.acgc.town;

public final class Abi {
    static {
        System.loadLibrary("abi");
    }

    private Abi() {}

    public static native String report();
}
