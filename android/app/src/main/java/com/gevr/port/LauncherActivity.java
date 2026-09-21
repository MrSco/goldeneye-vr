package com.gevr.port;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;

/**
 * Launcher that ensures the GoldenEye 007 USA ROM exists in
 * getExternalFilesDir(null)/data as ge.z64 or /sdcard/GEVR/ge.z64 before starting VR.
 */
public class LauncherActivity extends AppCompatActivity {
    public static final String ROM_FILE_NAME = "ge.z64";
    public static final String SDCARD_DIR = "/sdcard/GEVR";
    public static final String SDCARD_ROM = "/sdcard/GEVR/ge.z64";

    // Expected GoldenEye 007 (USA) ROM hash
    private static final String SHA256_USA = "2cdcec8a9f0cb6e36337f3ee39d8ad105dc8afa6ba1c02d466e8f5b771f9a162";
    private static final String MD5_USA = "70c52453472740bc835dd932a356396f";
    private static final long EXPECTED_SIZE = 12582912L; // 12 MB

    private int currentRomStatus = -1;

    private View missingRomView;
    private TextView infoText;
    private Button pickRomButton;
    private Button startButton;

    private final ActivityResultLauncher<String[]> romPicker =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), this::onRomPicked);

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ensureDataDir();

        setContentView(R.layout.activity_launcher);
        missingRomView = findViewById(R.id.missingRomContainer);
        infoText = findViewById(R.id.infoText);
        pickRomButton = findViewById(R.id.pickRomButton);
        startButton = findViewById(R.id.startButton);

        pickRomButton.setOnClickListener(v -> openRomPicker());
        startButton.setOnClickListener(v -> onStartClicked());

        refreshRomStatusUi();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshRomStatusUi();
    }

    private void ensureDataDir() {
        File dataDir = new File(getExternalFilesDir(null), "data");
        if (!dataDir.exists()) {
            dataDir.mkdirs();
        }
    }

    private File getRomDataDir() {
        return new File(getExternalFilesDir(null), "data");
    }

    private File getActiveRomFile() {
        File internalRom = new File(getRomDataDir(), ROM_FILE_NAME);
        if (internalRom.exists() && internalRom.length() > 0) {
            return internalRom;
        }
        File sdcardRom = new File(SDCARD_ROM);
        if (sdcardRom.exists() && sdcardRom.length() > 0) {
            return sdcardRom;
        }
        return internalRom;
    }

    private boolean romExists() {
        File target = getActiveRomFile();
        return target.exists() && target.length() > 0;
    }

    private void refreshRomStatusUi() {
        if (missingRomView != null) {
            missingRomView.setVisibility(View.VISIBLE);
        }

        if (!romExists()) {
            currentRomStatus = -1;
            infoText.setText("GoldenEye 007 ROM not found.\n"
                    + "Please select your USA GoldenEye 007 (.z64) ROM dump,\n"
                    + "or place it in /sdcard/GEVR/ge.z64.");
            setStartEnabled(false);
            return;
        }

        File target = getActiveRomFile();
        int hashStatus = checkRomHash(target);
        currentRomStatus = hashStatus;

        switch (hashStatus) {
            case 0:
                infoText.setText("ROM verified: USA GoldenEye 007 [!] (12 MB)\nReady to launch in Quest VR.");
                setStartEnabled(true);
                break;
            default:
                infoText.setText("ROM detected at " + target.getName() + " (" + (target.length() / 1024 / 1024) + " MB)\n"
                        + "Hash differs from verified USA dump. You may still try launching.");
                setStartEnabled(true);
                break;
        }
    }

    private void setStartEnabled(boolean enabled) {
        if (startButton != null) {
            startButton.setEnabled(enabled);
            startButton.setAlpha(enabled ? 1.0f : 0.5f);
        }
    }

    private void openRomPicker() {
        romPicker.launch(new String[]{"application/octet-stream", "*/*"});
    }

    private void onRomPicked(@Nullable Uri uri) {
        if (uri == null) {
            Toast.makeText(this, "No file selected", Toast.LENGTH_SHORT).show();
            return;
        }

        final int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Exception ignored) {
        }

        try {
            copyRomToAppData(uri);
        } catch (IOException e) {
            Toast.makeText(this, "Failed to copy ROM: " + e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        if (!romExists()) {
            Toast.makeText(this, "ROM copy failed", Toast.LENGTH_LONG).show();
            refreshRomStatusUi();
            return;
        }

        File target = getActiveRomFile();
        int hashStatus = checkRomHash(target);
        if (hashStatus == 0) {
            Toast.makeText(this, "GoldenEye ROM Verified!", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "ROM copied (custom hash)", Toast.LENGTH_SHORT).show();
        }
        refreshRomStatusUi();
    }

    private void onStartClicked() {
        if (!romExists()) {
            Toast.makeText(this, "Please select a ROM first", Toast.LENGTH_SHORT).show();
            return;
        }
        startGame();
    }

    private void copyRomToAppData(Uri sourceUri) throws IOException {
        File dataDir = getRomDataDir();
        if (!dataDir.exists()) {
            dataDir.mkdirs();
        }

        File target = new File(dataDir, ROM_FILE_NAME);

        try (InputStream in = getContentResolver().openInputStream(sourceUri);
             FileOutputStream out = new FileOutputStream(target)) {
            if (in == null) throw new IOException("Unable to open selected file");
            byte[] buf = new byte[16384];
            int read;
            while ((read = in.read(buf)) != -1) {
                out.write(buf, 0, read);
            }
            out.flush();
        }
    }

    private void startGame() {
        android.util.Log.i("GEVR", "Start clicked, launching GEVR MainActivity in VR mode");
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra(MainActivity.EXTRA_FROM_LAUNCHER, true);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK | Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        finish();
    }

    private int checkRomHash(File file) {
        try {
            if (file.length() == EXPECTED_SIZE) {
                String sha256 = computeSha256(file);
                if (SHA256_USA.equalsIgnoreCase(sha256)) return 0;
            }
            String md5 = computeMd5(file);
            if (MD5_USA.equalsIgnoreCase(md5)) return 0;
            return 1; // Different version / romhack
        } catch (Exception e) {
            return 1;
        }
    }

    private String computeSha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream is = new FileInputStream(file)) {
            byte[] buffer = new byte[32768];
            int read;
            while ((read = is.read(buffer)) > 0) {
                digest.update(buffer, 0, read);
            }
        }
        byte[] hash = digest.digest();
        StringBuilder sb = new StringBuilder();
        for (byte b : hash) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    private String computeMd5(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("MD5");
        try (InputStream is = new FileInputStream(file)) {
            byte[] buffer = new byte[32768];
            int read;
            while ((read = is.read(buffer)) > 0) {
                digest.update(buffer, 0, read);
            }
        }
        byte[] hash = digest.digest();
        StringBuilder sb = new StringBuilder();
        for (byte b : hash) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }
}
