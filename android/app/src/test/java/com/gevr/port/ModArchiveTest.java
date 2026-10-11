package com.gevr.port;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.util.zip.*;
import static org.junit.Assert.*;

public class ModArchiveTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    private File pack(String mesh, byte[] data) throws IOException {
        File zip = temp.newFile();
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(zip))) {
            for (int i = 0; i < 100; i++) {
                z.putNextEntry(new ZipEntry("GOLDENEYE/" + i + ".png"));
                z.write(new byte[]{1,2,3,4}); z.closeEntry();
            }
            if (mesh != null) {
                z.putNextEntry(new ZipEntry(mesh)); z.write(data); z.closeEntry();
            }
        }
        return zip;
    }

    @Test public void extractsBeanMeshesAndTexturesWithNamesIntact() throws Exception {
        File dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(pack("files/new/char/boilerbond/default.bin", new byte[]{'C','A','F','F',1}), dir, true, p -> {}));
        assertTrue(new File(dir, "files/new/char/boilerbond/default.bin").isFile());
    }
    @Test public void extractsOnlyAllowlistedBeanFonts() throws Exception {
        File zip = temp.newFile();
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(zip))) {
            for (int i = 0; i < 100; i++) {
                z.putNextEntry(new ZipEntry("GOLDENEYE/" + i + ".png"));
                z.write(new byte[]{1,2,3,4}); z.closeEntry();
            }
            for (String path : new String[]{"files/new/char/boilerbond/default.bin",
                    "files/misc/alps3/default.dt", "files/misc/doc0/default.dt",
                    "files/misc/other/default.dt"}) {
                z.putNextEntry(new ZipEntry(path));
                z.write(new byte[]{'C','A','F','F'}); z.closeEntry();
            }
        }
        File dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(zip, dir, true, p -> {}));
        assertTrue(new File(dir, "files/misc/alps3/default.dt").isFile());
        assertTrue(new File(dir, "files/misc/doc0/default.dt").isFile());
        assertFalse(new File(dir, "files/misc/other/default.dt").exists());
        dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(zip, dir, false, p -> {}));
        assertFalse(new File(dir, "files").exists());
    }
    @Test public void rejectsInvalidFontMagic() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack("files/misc/alps3/default.dt", new byte[]{1,2,3,4}), temp.newFolder(), true, p -> {}));
    }
    @Test public void rejectsTextureOnlyBeanPack() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack(null, null), temp.newFolder(), true, p -> {}));
    }
    @Test public void ordinaryPacksCannotInstallMeshes() throws Exception {
        File dir = temp.newFolder();
        assertEquals(100, ModArchive.unpack(pack("files/new/char/boilerbond/default.bin", new byte[]{'C','A','F','F'}), dir, false, p -> {}));
        assertFalse(new File(dir, "files").exists());
    }
    @Test public void rejectsInvalidMeshMagic() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack("files/new/char/boilerbond/default.bin", new byte[]{1,2,3,4}), temp.newFolder(), true, p -> {}));
    }
    @Test public void preventsZipTraversal() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack("GOLDENEYE/../../escape.png", new byte[]{1,2,3,4}), temp.newFolder(), true, p -> {}));
    }
    @Test public void cancelsDuringExtraction() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack(null, null), temp.newFolder(), false, p -> { throw new IOException("cancelled"); }));
    }
    @Test public void requiresBondMarker() throws Exception {
        assertThrows(IOException.class, () -> ModArchive.unpack(pack("files/new/gun/silverPPK/default.bin", new byte[]{'C','A','F','F'}), temp.newFolder(), true, p -> {}));
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
        assertThrows(IOException.class, () -> ModArchive.unpack(zip, temp.newFolder(), false, p -> {}));
    }
}
