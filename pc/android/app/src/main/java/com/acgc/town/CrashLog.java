package com.acgc.town;

import android.content.Context;
import android.content.Intent;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

/** The native crash handler writes acgc-crash.log. The launcher shows it before the game starts. */
public final class CrashLog {
    public static final String FILE_NAME = "acgc-crash.log";

    private CrashLog() {}

    public static File find(Context context) {
        File ext = context.getExternalFilesDir(null);
        if (ext != null) {
            File log = new File(ext, FILE_NAME);
            if (log.isFile() && log.length() > 0) return log;
        }
        File internal = new File(context.getFilesDir(), FILE_NAME);
        if (internal.isFile() && internal.length() > 0) return internal;
        return null;
    }

    public static String readTail(File log) {
        if (log == null || !log.isFile()) return "";
        int cap = 120000;
        try (FileInputStream in = new FileInputStream(log)) {
            long len = log.length();
            long skip = len > cap ? len - cap : 0;
            if (skip > 0) in.skip(skip);
            byte[] buf = new byte[(int)(len - skip)];
            int n = 0;
            while (n < buf.length) {
                int r = in.read(buf, n, buf.length - n);
                if (r < 0) break;
                n += r;
            }
            String text = new String(buf, 0, n, StandardCharsets.UTF_8);
            return "Log file:\n" + log.getAbsolutePath() + "\n\n" + text;
        } catch (Exception e) {
            return "Could not read " + log.getAbsolutePath() + "\n" + e;
        }
    }

    public static Intent share(String text) {
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, "ACGC Town crash log");
        send.putExtra(Intent.EXTRA_TEXT, text);
        return Intent.createChooser(send, "Share log");
    }

    public static void dismiss(File log) {
        if (log == null || !log.isFile()) return;
        File prev = new File(log.getParentFile(), "acgc-crash.prev");
        if (prev.exists() && !prev.delete()) {
            log.delete();
            return;
        }
        if (!log.renameTo(prev)) log.delete();
    }
}
