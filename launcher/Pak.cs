// Minimal read-only zip reader for Space Marine 2 paks (zip64-aware, stored + deflate), including zips stored inside zips.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

class Pak : IDisposable {
    public class Entry { public string Name; public long LocalOffset, CompSize, Size; public int Method; }

    readonly Stream s;
    readonly long baseOff;
    public readonly Dictionary<string, Entry> Entries = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);

    public Pak(string path) : this(new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 16), 0, -1) { }

    Pak(Stream s, long baseOff, long length) {
        this.s = s; this.baseOff = baseOff;
        if (length < 0) length = s.Length - baseOff;
        ReadDirectory(length);
    }

    // A stored (uncompressed) zip inside this one, read in place.
    public Pak OpenInner(string name) {
        Entry e = Entries[name];
        if (e.Method != 0) throw new InvalidDataException(name + " is compressed; expected stored");
        return new Pak(s, baseOff + DataOffset(e), e.Size);
    }

    public bool Has(string name) { return Entries.ContainsKey(name); }

    public byte[] Read(string name) { return Read(Entries[name]); }

    public byte[] Read(Entry e) {
        s.Position = baseOff + DataOffset(e);
        byte[] comp = ReadExact((int)e.CompSize);
        if (e.Method == 0) return comp;
        if (e.Method != 8) throw new InvalidDataException(e.Name + ": compression " + e.Method);
        byte[] outp = new byte[e.Size];
        using (var ds = new DeflateStream(new MemoryStream(comp), CompressionMode.Decompress)) {
            int got = 0;
            while (got < outp.Length) { int n = ds.Read(outp, got, outp.Length - got); if (n <= 0) break; got += n; }
        }
        return outp;
    }

    long DataOffset(Entry e) {
        s.Position = baseOff + e.LocalOffset;
        byte[] h = ReadExact(30);
        if (BitConverter.ToUInt32(h, 0) != 0x04034b50) throw new InvalidDataException("bad local header " + e.Name);
        return e.LocalOffset + 30 + BitConverter.ToUInt16(h, 26) + BitConverter.ToUInt16(h, 28);
    }

    byte[] ReadExact(int n) {
        byte[] b = new byte[n]; int got = 0;
        while (got < n) { int r = s.Read(b, got, n - got); if (r <= 0) throw new EndOfStreamException(); got += r; }
        return b;
    }

    void ReadDirectory(long length) {
        int tail = (int)Math.Min(length, 65536 + 22);
        s.Position = baseOff + length - tail;
        byte[] t = ReadExact(tail);
        int eocd = -1;
        for (int i = tail - 22; i >= 0; i--) if (BitConverter.ToUInt32(t, i) == 0x06054b50) { eocd = i; break; }
        if (eocd < 0) throw new InvalidDataException("not a zip");
        long count = BitConverter.ToUInt16(t, eocd + 10);
        long cdSize = BitConverter.ToUInt32(t, eocd + 12);
        long cdOff = BitConverter.ToUInt32(t, eocd + 16);
        // zip64 end-of-directory locator sits right before the classic record
        if (eocd >= 20 && BitConverter.ToUInt32(t, eocd - 20) == 0x07064b50) {
            long z64 = BitConverter.ToInt64(t, eocd - 20 + 8);
            s.Position = baseOff + z64;
            byte[] r = ReadExact(56);
            count = BitConverter.ToInt64(r, 32); cdSize = BitConverter.ToInt64(r, 40); cdOff = BitConverter.ToInt64(r, 48);
        }
        s.Position = baseOff + cdOff;
        byte[] cd = ReadExact((int)cdSize);
        int p = 0;
        for (long i = 0; i < count; i++) {
            if (BitConverter.ToUInt32(cd, p) != 0x02014b50) throw new InvalidDataException("bad central directory");
            var e = new Entry();
            e.Method = BitConverter.ToUInt16(cd, p + 10);
            e.CompSize = BitConverter.ToUInt32(cd, p + 20);
            e.Size = BitConverter.ToUInt32(cd, p + 24);
            int nl = BitConverter.ToUInt16(cd, p + 28), xl = BitConverter.ToUInt16(cd, p + 30), cl = BitConverter.ToUInt16(cd, p + 32);
            e.LocalOffset = BitConverter.ToUInt32(cd, p + 42);
            e.Name = Encoding.UTF8.GetString(cd, p + 46, nl);
            int x = p + 46 + nl, xe = x + xl;
            while (x + 4 <= xe) {
                int id = BitConverter.ToUInt16(cd, x), sz = BitConverter.ToUInt16(cd, x + 2), q = x + 4;
                if (id == 1) {
                    if (e.Size == 0xFFFFFFFF) { e.Size = BitConverter.ToInt64(cd, q); q += 8; }
                    if (e.CompSize == 0xFFFFFFFF) { e.CompSize = BitConverter.ToInt64(cd, q); q += 8; }
                    if (e.LocalOffset == 0xFFFFFFFF) { e.LocalOffset = BitConverter.ToInt64(cd, q); }
                }
                x += 4 + sz;
            }
            Entries[e.Name] = e;
            p += 46 + nl + xl + cl;
        }
    }

    public void Dispose() { if (baseOff == 0) s.Dispose(); }
}
