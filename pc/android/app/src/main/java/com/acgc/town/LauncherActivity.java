package com.acgc.town;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/** First screen. The disc is chosen with the system file picker and copied into app-private storage. It is not in the APK. */
public class LauncherActivity extends Activity {
    private static final int PICK = 1;

    private File romFile() {
        return new File(getFilesDir(), "gafe01.iso");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);
        TextView text = new TextView(this);
        text.setText("ACGC Town needs your own USA GAFE01 disc image. Nothing from the game is in this app.\n\n"
                + "ABI: " + Abi.report() + "\n\n"
                + (romFile().isFile() ? "A disc image is already saved on this device." : "No disc image saved yet."));
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
        play.setOnClickListener(v -> startActivity(new Intent(this, TownActivity.class)));
        root.addView(text);
        root.addView(pick);
        root.addView(play);
        setContentView(root);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(romFile())) {
            byte[] buf = new byte[1024 * 1024];
            int n;
            while (in != null && (n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (Exception e) {
            return;
        }
        recreate();
    }
}
