package com.gevr.port;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.util.zip.*;
import static org.junit.Assert.*;

/* The texture packs' downloads. Bean is never downloaded: its import from the
 * player's own copy is port/src/gevr_bean_import.c, tested by
 * port/tests/test_bean_import.py. */
public class ModArchiveTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    private File pack(String extra, byte[] data) throws IOException {
        File zip = temp.newFile();
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(zip))) {
            for (int i = 0; i < 100; i++) {
                z.putNextEntry(new ZipEntry("GOLDENEYE/" + i + ".png"));
                z.write(new byte[]{1,2,3,4}); z.closeEntry();
            }
            if (extra != null) {
                z.putNextEntry(new ZipEntry(extra)); z.write(data); z.closeEntry();
            }
        }
        return zip;
    }

    @Test public void extractsTexturesWithNamesIntact() throws Exception {
        File dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(pack(null, null), dir, p -> {}));
        assertTrue(new File(dir, "GOLDENEYE/42.png").isFile());
    }
    @Test public void texturePacksCannotInstallMeshes() throws Exception {
        File dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(pack("files/new/char/boilerbond/default.bin", new byte[]{'C','A','F','F'}), dir, p -> {}));
        assertFalse(new File(dir, "files").exists());
    }
    @Test public void rejectsTooFewTextures() throws Exception {
        File zip = temp.newFile();
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(zip))) {
            z.putNextEntry(new ZipEntry("GOLDENEYE/0.png"));
            z.write(new byte[]{1,2,3,4}); z.closeEntry();
        }
        assertThrows(IOException.class, () -> ModArchive.unpack(zip, temp.newFolder(), p -> {}));
    }
    @Test public void preventsZipTraversal() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack("GOLDENEYE/../../escape.png", new byte[]{1,2,3,4}), temp.newFolder(), p -> {}));
    }
    @Test public void cancelsDuringExtraction() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack(null, null), temp.newFolder(), p -> { throw new IOException("cancelled"); }));
    }
    @Test public void rejectsCorruptCrc() throws Exception {
        File zip = pack(null, null);
        try (RandomAccessFile file = new RandomAccessFile(zip, "rw")) {
            byte[] bytes = new byte[(int) file.length()]; file.readFully(bytes);
            for (int i = 0; i < bytes.length - 20; i++) {
                if (bytes[i] == 'P' && bytes[i+1] == 'K' && bytes[i+2] == 1 && bytes[i+3] == 2) {
                    file.seek(i + 16); file.write(bytes[i+16] ^ 1); break;
                }
            }
        }
        assertThrows(IOException.class, () -> ModArchive.unpack(zip, temp.newFolder(), p -> {}));
    }
}
