package com.acgc.town;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/** First screen. The disc is chosen with the system file picker and copied into app-private storage. It is not in the APK. */
public class LauncherActivity extends Activity {
    private static final int PICK = 1;
    private String copyError = "";

    private File romFile() {
        return new File(getFilesDir(), "gafe01.iso");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        File crash = CrashLog.find(this);
        if (crash != null) {
            showCrash(crash);
            return;
        }
        showLauncher();
    }

    private void showCrash(File crash) {
        String text = CrashLog.readTail(crash);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);
        TextView title = new TextView(this);
        title.setText("The last run crashed. Share the log, then start again.");
        TextView body = new TextView(this);
        body.setText(text);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        Button share = new Button(this);
        share.setText("Share log");
        share.setOnClickListener(v -> startActivity(CrashLog.share(text)));
        Button cont = new Button(this);
        cont.setText("Continue");
        cont.setOnClickListener(v -> {
            CrashLog.dismiss(crash);
            showLauncher();
        });
        root.addView(title);
        root.addView(scroll, scrollLp);
        root.addView(share);
        root.addView(cont);
        setContentView(root);
        SafeInsets.install(this);
    }

    private void showLauncher() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);
        TextView text = new TextView(this);
        String disc = romFile().isFile() && romFile().length() > 1024
                ? "A disc image is already saved on this device."
                : "No disc image saved yet.";
        text.setText("ACGC Town needs your own USA GAFE01 disc image. Nothing from the game is in this app.\n\n"
                + "ABI: " + Abi.report() + "\n\n"
                + disc
                + (copyError.isEmpty() ? "" : "\n\n" + copyError));
        Button pick = new Button(this);
        pick.setText("Choose GAFE01 image");
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK);
        });
        Button play = new Button(this);
        play.setText("Start");
        play.setOnClickListener(v -> {
            File rom = romFile();
            if (!rom.isFile() || rom.length() < 1024) {
                copyError = "Choose a GAFE01 disc image first. The game opens the copy in app storage, not a content:// URI.";
                showLauncher();
                return;
            }
            startActivity(new Intent(this, TownActivity.class));
        });
        root.addView(text);
        root.addView(pick);
        root.addView(play);
        setContentView(root);
        SafeInsets.install(this);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(romFile())) {
            if (in == null) {
                copyError = "The picker did not open a readable stream for " + uri;
                showLauncher();
                return;
            }
            byte[] buf = new byte[1024 * 1024];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                total += n;
            }
            if (total < 1024) {
                copyError = "The chosen file copied only " + total + " bytes. Pick the GAFE01 disc image again.";
            } else {
                copyError = "";
            }
        } catch (Exception e) {
            copyError = "Could not copy the disc into app storage: " + e;
        }
        showLauncher();
    }
}
