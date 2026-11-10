package com.acgc.town;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/**
 * Crash notes that do not depend on native code. A launch marker is committed
 * before System.loadLibrary. If the process dies, the next launch reads logcat
 * for that pid and shows the Share log screen.
 */
public final class LaunchTrace {
    private static final String TAG = "ACGC";
    private static final String PREFS = "acgc_launch";
    private static final String KEY_IN_PROGRESS = "in_progress";
    private static final String KEY_PID = "pid";
    private static final String KEY_TIME = "time";
    private static final String LOGDIR_FILE = "acgc-logdir.txt";
    private static final int TAIL = 120000;

    private static volatile boolean sHandlerInstalled;

    private LaunchTrace() {}

    public static void installHandler(Context context) {
        Context app = context.getApplicationContext();
        if (sHandlerInstalled) return;
        sHandlerInstalled = true;
        Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            String text = "Java uncaught exception on " + thread.getName() + "\n"
                    + Log.getStackTraceString(error);
            Log.e(TAG, text);
            writeCrash(app, text);
            if (previous != null) previous.uncaughtException(thread, error);
            else android.os.Process.killProcess(android.os.Process.myPid());
        });
    }

    /** Call immediately before System.loadLibrary. commit() waits for the disk write. */
    public static void mark(Context context) {
        installHandler(context);
        File dir = logDir(context);
        writeLogDirFile(context, dir);
        boolean ok = prefs(context).edit()
                .putBoolean(KEY_IN_PROGRESS, true)
                .putInt(KEY_PID, android.os.Process.myPid())
                .putLong(KEY_TIME, System.currentTimeMillis())
                .commit();
        Log.i(TAG, "launch-in-progress pid=" + android.os.Process.myPid() + " committed=" + ok
                + " logdir=" + dir.getAbsolutePath());
    }

    public static void clear(Context context) {
        boolean ok = prefs(context).edit().putBoolean(KEY_IN_PROGRESS, false).commit();
        Log.i(TAG, "launch-in-progress cleared committed=" + ok);
    }

    public static boolean wasInterrupted(Context context) {
        SharedPreferences p = prefs(context);
        if (!p.getBoolean(KEY_IN_PROGRESS, false)) return false;
        int pid = p.getInt(KEY_PID, -1);
        return pid > 0 && pid != android.os.Process.myPid();
    }

    /** Read logcat for the interrupted launch and store it next to the native logs. */
    public static File captureInterrupted(Context context) {
        SharedPreferences p = prefs(context);
        int pid = p.getInt(KEY_PID, -1);
        long when = p.getLong(KEY_TIME, 0);
        StringBuilder sb = new StringBuilder();
        sb.append("ACGC Town launch did not finish\n");
        sb.append("previous pid ").append(pid).append('\n');
        sb.append("marked at ").append(when).append('\n');
        sb.append("this pid ").append(android.os.Process.myPid()).append("\n\n");
        String byPid = logcat(new String[] {"logcat", "-d", "-v", "threadtime", "--pid=" + pid}, false);
        if (byPid.trim().isEmpty() || byPid.contains("logcat failed")) {
            sb.append("logcat --pid was empty or failed; filtered logcat follows\n");
            if (!byPid.trim().isEmpty()) {
                sb.append(byPid).append('\n');
            }
            sb.append(logcat(new String[] {"logcat", "-d", "-v", "threadtime"}, true));
        } else {
            sb.append(byPid);
        }
        sb.append("\n\n--- native and previous files ---\n");
        appendExisting(sb, context);
        File out = writeCrash(context, sb.toString());
        clear(context);
        return out;
    }

    /** Last session log plus a fresh logcat slice, for the launcher button. */
    public static String debugText(Context context) {
        StringBuilder sb = new StringBuilder();
        sb.append("ACGC Town debug info\n");
        sb.append("pid ").append(android.os.Process.myPid()).append('\n');
        SharedPreferences p = prefs(context);
        sb.append("launch-in-progress ").append(p.getBoolean(KEY_IN_PROGRESS, false));
        sb.append(" savedPid ").append(p.getInt(KEY_PID, -1)).append('\n');
        sb.append("logdir ").append(logDir(context).getAbsolutePath()).append("\n\n");
        appendExisting(sb, context);
        sb.append("\n--- logcat (filtered) ---\n");
        sb.append(logcat(new String[] {"logcat", "-d", "-v", "threadtime"}, true));
        String text = sb.toString();
        writeCrash(context, text, "acgc-debug.txt");
        if (text.length() > TAIL) text = text.substring(text.length() - TAIL);
        return text;
    }

    public static void note(Context context, String text) {
        Log.e(TAG, text);
        writeCrash(context, text);
    }

    private static SharedPreferences prefs(Context context) {
        return context.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static File logDir(Context context) {
        File ext = context.getExternalFilesDir(null);
        if (ext != null) {
            if (!ext.isDirectory() && !ext.mkdirs()) {
                Log.w(TAG, "external files dir was not created: " + ext);
            }
            if (ext.isDirectory()) return ext;
        }
        return context.getFilesDir();
    }

    private static void writeLogDirFile(Context context, File dir) {
        File marker = new File(context.getFilesDir(), LOGDIR_FILE);
        try (FileOutputStream out = new FileOutputStream(marker)) {
            out.write(dir.getAbsolutePath().getBytes(StandardCharsets.UTF_8));
            out.write('\n');
            out.flush();
            out.getFD().sync();
        } catch (Exception e) {
            Log.e(TAG, "could not write " + marker, e);
        }
    }

    private static File writeCrash(Context context, String text) {
        return writeCrash(context, text, CrashLog.FILE_NAME);
    }

    private static File writeCrash(Context context, String text, String name) {
        File dir = logDir(context);
        File out = new File(dir, name);
        try (FileOutputStream fos = new FileOutputStream(out)) {
            fos.write(text.getBytes(StandardCharsets.UTF_8));
            fos.flush();
            fos.getFD().sync();
        } catch (Exception e) {
            Log.e(TAG, "could not write " + out, e);
            out = new File(context.getFilesDir(), name);
            try (FileOutputStream fos = new FileOutputStream(out)) {
                fos.write(text.getBytes(StandardCharsets.UTF_8));
                fos.flush();
                fos.getFD().sync();
            } catch (Exception again) {
                Log.e(TAG, "could not write " + out, again);
            }
        }
        return out;
    }

    private static void appendExisting(StringBuilder sb, Context context) {
        appendFile(sb, new File(logDir(context), "acgc-session.log"));
        appendFile(sb, new File(logDir(context), CrashLog.FILE_NAME));
        appendFile(sb, new File(context.getFilesDir(), "acgc-session.log"));
        appendFile(sb, new File(context.getFilesDir(), CrashLog.FILE_NAME));
    }

    private static void appendFile(StringBuilder sb, File file) {
        sb.append("\nfile ").append(file.getAbsolutePath()).append('\n');
        if (!file.isFile()) {
            sb.append("(missing)\n");
            return;
        }
        try (FileInputStream in = new FileInputStream(file)) {
            long len = file.length();
            long skip = len > TAIL ? len - TAIL : 0;
            if (skip > 0) in.skip(skip);
            byte[] buf = new byte[(int) (len - skip)];
            int n = 0;
            while (n < buf.length) {
                int r = in.read(buf, n, buf.length - n);
                if (r < 0) break;
                n += r;
            }
            sb.append(new String(buf, 0, n, StandardCharsets.UTF_8)).append('\n');
        } catch (Exception e) {
            sb.append("read failed: ").append(e).append('\n');
        }
    }

    private static boolean keepLine(String line) {
        return line.contains("ACGC")
                || line.contains(" DEBUG")
                || line.contains(" libc")
                || line.contains("AndroidRuntime")
                || line.contains(" linker")
                || line.contains("Fatal signal")
                || line.contains("UnsatisfiedLinkError")
                || line.contains("libc++");
    }

    private static String logcat(String[] cmd, boolean filter) {
        Process proc = null;
        try {
            proc = new ProcessBuilder(cmd).redirectErrorStream(true).start();
            Process waiting = proc;
            ByteArrayOutputStream raw = new ByteArrayOutputStream();
            Thread reader = new Thread(() -> {
                byte[] buf = new byte[4096];
                try {
                    int n;
                    while ((n = waiting.getInputStream().read(buf)) > 0) {
                        if (raw.size() < 400000) raw.write(buf, 0, n);
                    }
                } catch (Exception ignored) {
                }
            }, "acgc-logcat");
            reader.start();
            boolean done = waiting.waitFor(6, TimeUnit.SECONDS);
            if (!done) waiting.destroyForcibly();
            reader.join(1000);
            String text = raw.toString(StandardCharsets.UTF_8.name());
            if (!done) text = text + "\n(logcat timed out)\n";
            if (!filter) return text;
            StringBuilder kept = new StringBuilder();
            int count = 0;
            for (String line : text.split("\n")) {
                if (!keepLine(line)) continue;
                kept.append(line).append('\n');
                if (++count >= 2000) {
                    kept.append("(truncated)\n");
                    break;
                }
            }
            if (count == 0) {
                kept.append("(no matching logcat lines)\n");
                if (text.contains("Permission") || text.contains("denied")) {
                    kept.append(text.length() > 2000 ? text.substring(0, 2000) : text);
                }
            }
            return kept.toString();
        } catch (Exception e) {
            return "logcat failed: " + e + "\n";
        } finally {
            if (proc != null) proc.destroy();
        }
    }
}
