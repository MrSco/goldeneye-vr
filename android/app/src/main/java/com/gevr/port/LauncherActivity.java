package com.gevr.port;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.widget.SwitchCompat;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The app's entry panel (a 2D window on the Quest): shows the GoldenEye 007 ROM
 * in use (getExternalFilesDir(null)/data/ge.z64 or /sdcard/GEVR/ge.z64) and lets
 * the player pick one, choose stereo VR or the flat screen, the right-stick turn
 * style and the comfort vignette, then starts the immersive MainActivity.
 *
 * The choices live in the game's own settings file, data/goldeneye-vr.ini
 * (port/vr/vr_settings.cpp reads it at start): PlayMode, SnapTurn and
 * ComfortVignette are rewritten in place, everything else in the file is left
 * alone. The vignette strength is remembered in the launcher's preferences so
 * switching it off and on keeps the chosen level.
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

    private static final String INI_NAME = "goldeneye-vr.ini";
    private static final String PREFS = "launcher";
    private static final String PREF_VIGNETTE_LEVEL = "vignetteLevel";

    private View missingRomView;
    private TextView infoText;
    private TextView romPathText;
    private Button pickRomButton;
    private Button startButton;
    private RadioGroup modeGroup;
    private RadioGroup turnGroup;
    private SwitchCompat vignetteSwitch;
    private SeekBar vignetteSeek;
    private TextView vignetteValue;

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

        romPathText = findViewById(R.id.romPathText);
        modeGroup = findViewById(R.id.modeGroup);
        turnGroup = findViewById(R.id.turnGroup);
        vignetteSwitch = findViewById(R.id.vignetteSwitch);
        vignetteSeek = findViewById(R.id.vignetteSeek);
        vignetteValue = findViewById(R.id.vignetteValue);

        pickRomButton.setOnClickListener(v -> openRomPicker());
        startButton.setOnClickListener(v -> onStartClicked());

        loadOptionsIntoUi();
        vignetteSwitch.setOnCheckedChangeListener((b, on) -> updateVignetteUi());
        vignetteSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int v, boolean fromUser) { updateVignetteUi(); }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) { }
        });

        refreshRomStatusUi();
    }

    // ---------------------------------------------------------------- options

    private File getIniFile() {
        return new File(getRomDataDir(), INI_NAME);
    }

    /** key=value from goldeneye-vr.ini, or null. */
    private List<String> readIniLines() throws IOException {
        List<String> lines = new ArrayList<>();
        File ini = getIniFile();
        if (!ini.exists()) return lines;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(ini), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) lines.add(line);
        }
        return lines;
    }

    private String readIniValue(String key) {
        try {
            for (String line : readIniLines()) {
                if (line.startsWith(key + "=")) {
                    return line.substring(key.length() + 1).trim();
                }
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    private float readIniFloat(String key, float fallback) {
        String v = readIniValue(key);
        if (v == null) return fallback;
        try {
            return Float.parseFloat(v);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** Rewrite (or add) the given keys in goldeneye-vr.ini, leaving every other line as it is. */
    private void writeIniValues(String[] keys, String[] values) throws IOException {
        File ini = getIniFile();
        List<String> lines = readIniLines();
        if (lines.isEmpty()) lines.add("[VR]");
        for (int k = 0; k < keys.length; k++) {
            boolean found = false;
            for (int i = 0; i < lines.size(); i++) {
                if (lines.get(i).startsWith(keys[k] + "=")) {
                    lines.set(i, keys[k] + "=" + values[k]);
                    found = true;
                }
            }
            if (!found) lines.add(keys[k] + "=" + values[k]);
        }
        try (FileOutputStream out = new FileOutputStream(ini)) {
            out.write((android.text.TextUtils.join("\n", lines) + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    private void loadOptionsIntoUi() {
        // Display: stereo unless the player last chose the screen.
        String mode = readIniValue("PlayMode");
        ((RadioButton) findViewById("0".equals(mode) ? R.id.modeScreen : R.id.modeStereo)).setChecked(true);

        // Turning: 0 = smooth, else the snap angle.
        int snap = Math.round(readIniFloat("SnapTurn", 0.0f));
        int turnId = R.id.turnSmooth;
        if (snap >= 80) turnId = R.id.turnSnap90;
        else if (snap >= 40) turnId = R.id.turnSnap45;
        else if (snap > 0) turnId = R.id.turnSnap30;
        ((RadioButton) findViewById(turnId)).setChecked(true);

        // Comfort vignette: the ini holds the strength in use (0 = off); the
        // launcher remembers the last non-zero strength for the slider.
        float vig = readIniFloat("ComfortVignette", 0.0f);
        float remembered = getSharedPreferences(PREFS, MODE_PRIVATE).getFloat(PREF_VIGNETTE_LEVEL, 0.5f);
        vignetteSwitch.setChecked(vig > 0.0f);
        vignetteSeek.setProgress(Math.round((vig > 0.0f ? vig : remembered) * 100.0f));
        updateVignetteUi();
    }

    private void updateVignetteUi() {
        boolean on = vignetteSwitch.isChecked();
        vignetteSeek.setEnabled(on);
        vignetteSeek.setAlpha(on ? 1.0f : 0.4f);
        vignetteValue.setText(on ? (vignetteSeek.getProgress() + "%") : "off");
    }

    private void saveOptionsFromUi() {
        String playMode = modeGroup.getCheckedRadioButtonId() == R.id.modeScreen ? "0" : "1";

        int turnId = turnGroup.getCheckedRadioButtonId();
        String snap = "0.0";
        if (turnId == R.id.turnSnap30) snap = "30.0";
        else if (turnId == R.id.turnSnap45) snap = "45.0";
        else if (turnId == R.id.turnSnap90) snap = "90.0";

        float level = Math.max(0.05f, vignetteSeek.getProgress() / 100.0f);
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putFloat(PREF_VIGNETTE_LEVEL, level).apply();
        String vig = vignetteSwitch.isChecked() ? String.format(Locale.US, "%.2f", level) : "0.00";

        try {
            writeIniValues(new String[]{"PlayMode", "SnapTurn", "ComfortVignette"},
                           new String[]{playMode, snap, vig});
        } catch (IOException e) {
            Toast.makeText(this, "Could not save settings: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
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
            if (romPathText != null) romPathText.setText("");
            setStartEnabled(false);
            return;
        }

        File target = getActiveRomFile();
        if (romPathText != null) romPathText.setText("In use: " + target.getAbsolutePath());
        int hashStatus = checkRomHash(target);
        currentRomStatus = hashStatus;

        switch (hashStatus) {
            case 0:
                infoText.setText("ROM verified: USA GoldenEye 007 [!] (12 MB)");
                setStartEnabled(true);
                break;
            default:
                infoText.setText("ROM found (" + (target.length() / 1024 / 1024) + " MB), but it is not the verified USA dump.\n"
                        + "You may still try launching.");
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
        saveOptionsFromUi();
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
