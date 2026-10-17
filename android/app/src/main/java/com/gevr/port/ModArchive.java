package com.gevr.port;

import java.io.*;
import java.util.*;
import java.util.zip.*;

/** Bounded pack extraction, shared by the downloader and JVM regression checks. */
final class ModArchive {
    interface Progress { void entry(int percent) throws IOException; }

    /** A texture pack: its PNGs, at least 100 of them. */
    static int unpack(File zip, File dir, Progress progress) throws IOException {
        return unpack(zip, dir, new String[] { ".png" }, 100, progress);
    }

    /** The entries whose names end in one of kinds (lower case), at least least of them. */
    static int unpack(File zip, File dir, String[] kinds, int least, Progress progress) throws IOException {
        String base = dir.getCanonicalPath() + File.separator;
        Set<String> paths = new HashSet<>();
        long extracted = 0;
        int textures = 0, seen = 0;
        try (ZipFile z = new ZipFile(zip)) {
            Enumeration<? extends ZipEntry> entries = z.entries();
            byte[] buffer = new byte[65536];
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                progress.entry(Math.min(99, ++seen * 100 / Math.max(1, z.size())));
                String name = entry.getName();
                boolean wanted = false;
                for (String kind : kinds) wanted |= name.toLowerCase(Locale.ROOT).endsWith(kind);
                if (entry.isDirectory() || !wanted) continue;
                File file = new File(dir, name);
                String path = file.getCanonicalPath();
                if (!path.startsWith(base) || !paths.add(path)) throw new IOException("bad or duplicate path: " + name);
                if (entry.getSize() < 4 || entry.getSize() > (64L << 20)) throw new IOException("invalid entry size: " + name);
                if (!file.getParentFile().isDirectory() && !file.getParentFile().mkdirs()) throw new IOException("could not create pack folder");
                CRC32 crc = new CRC32();
                long bytes = 0;
                try (InputStream in = z.getInputStream(entry); OutputStream out = new FileOutputStream(file)) {
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        progress.entry(Math.min(99, seen * 100 / Math.max(1, z.size())));
                        bytes += n;
                        extracted += n;
                        if (bytes > entry.getSize() || extracted > (4L << 30)) throw new IOException("pack exceeds extraction limit");
                        crc.update(buffer, 0, n);
                        out.write(buffer, 0, n);
                    }
                }
                if (bytes != entry.getSize() || crc.getValue() != entry.getCrc()) throw new IOException("damaged archive entry: " + name);
                textures++;
            }
        }
        if (textures < least) throw new IOException("download is missing pack files");
        return textures;
    }
}
