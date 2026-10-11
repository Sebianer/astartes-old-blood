// Reads the player's own Space Marine 2 files and writes what the mod needs into %LOCALAPPDATA%\AstartesOldBlood\content:
//   card_<class>.bgra  raw 396x630 BGRA card art
//   voice\<class>_<role>_<n>.wav, sfx\<sfx>.wav   decoded with vgmstream-cli
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

static class Content {
    public const int CardW = 396, CardH = 630;
    public const string Version = "1";
    const int MaxTakes = 4;

    public static readonly string Root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AstartesOldBlood");
    public static readonly string Dir = Path.Combine(Root, "content");

    public static bool Ready() {
        string f = Path.Combine(Dir, "ready.txt");
        return File.Exists(f) && File.ReadAllText(f).Trim() == Version;
    }

    public static string Card(string classId) { return Path.Combine(Dir, "card_" + classId + ".bgra"); }

    public static string[] VoiceTakes(string classId, string role) {
        string d = Path.Combine(Dir, "voice");
        return Directory.Exists(d) ? Directory.GetFiles(d, classId + "_" + role + "_*.wav") : new string[0];
    }

    static string PakDir(string sm2) { return Path.Combine(sm2, @"client_pc\root\paks\client\default"); }

    public static void Extract(string sm2, Action<string> progress) {
        string paks = PakDir(sm2);
        if (!Directory.Exists(paks)) throw new Exception("Space Marine 2 files not found in " + sm2);
        string tmp = Path.Combine(Root, "tmp");
        if (Directory.Exists(tmp)) Directory.Delete(tmp, true);
        Directory.CreateDirectory(tmp);
        Directory.CreateDirectory(Path.Combine(Dir, "voice"));
        Directory.CreateDirectory(Path.Combine(Dir, "sfx"));
        File.Delete(Path.Combine(Dir, "ready.txt"));

        progress("Reading class cards");
        var wantCards = GenData.Classes.ToDictionary(c => "textures/ui/class_icons/cards/class_" + c.Sm2Id + ".asset/class_" + c.Sm2Id + "_0.pct_mip", c => c.Id);
        foreach (string pakPath in Directory.GetFiles(paks, "default_pct_*.pak")) {
            if (wantCards.Count == 0) break;
            using (var pak = new Pak(pakPath)) {
                foreach (var kv in wantCards.ToList()) {
                    if (!pak.Has(kv.Key)) continue;
                    byte[] px = pak.Read(kv.Key);
                    if (px.Length != CardW * CardH * 4) throw new Exception("unexpected card size " + kv.Key);
                    File.WriteAllBytes(Card(kv.Value), px);
                    wantCards.Remove(kv.Key);
                }
            }
        }
        if (wantCards.Count > 0) throw new Exception("class cards missing: " + string.Join(", ", wantCards.Values));

        progress("Reading voice lines");
        using (var pak = new Pak(Path.Combine(paks, "default_sound_english.pak")))
        using (var vo = pak.OpenInner("sounds/desktop/external_sources_vorbis_english.zip")) {
            var byBase = new Dictionary<string, List<Pak.Entry>>();
            foreach (var e in vo.Entries.Values) {
                string n = Path.GetFileName(e.Name);
                if (!n.StartsWith("vo_chatter_") || !n.EndsWith(".wem")) continue;
                int us = n.LastIndexOf('_');
                string b = n.Substring(0, us);
                List<Pak.Entry> l;
                if (!byBase.TryGetValue(b, out l)) byBase[b] = l = new List<Pak.Entry>();
                l.Add(e);
            }
            foreach (var c in GenData.Classes) {
                var roles = GenData.Voice.Select(v => new[] { v[0], v[1] }).ToList();
                roles.Add(new[] { "ability", c.AbilityVoice });
                foreach (var r in roles) {
                    List<Pak.Entry> takes;
                    if (!byBase.TryGetValue("vo_chatter_" + c.VoiceId + "_" + r[1], out takes)) continue;
                    int n = 0;
                    foreach (var e in takes.OrderBy(x => x.Name).Take(MaxTakes))
                        File.WriteAllBytes(Path.Combine(tmp, "v_" + c.Id + "_" + r[0] + "_" + (++n) + ".wem"), vo.Read(e));
                }
            }
        }

        progress("Reading sound effects");
        var banks = new Dictionary<string, Bank>();
        foreach (var s in GenData.Sfx) {
            Bank bank;
            if (!banks.TryGetValue(s.Bank, out bank)) banks[s.Bank] = bank = Bank.Load(paks, s.Bank);
            byte[] wem = null;
            foreach (uint m in s.Media) { wem = bank.Media(m); if (wem != null) break; }
            if (wem != null) File.WriteAllBytes(Path.Combine(tmp, "s_" + s.Id + ".wem"), wem);
        }

        progress("Decoding audio");
        string cli = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"vgmstream\vgmstream-cli.exe");
        var wems = Directory.GetFiles(tmp, "*.wem").Select(Path.GetFileName).ToList();
        for (int i = 0; i < wems.Count; i += 60) {
            var psi = new ProcessStartInfo(cli, "-i -o \"?f.wav\" " + string.Join(" ", wems.Skip(i).Take(60).Select(w => "\"" + w + "\"")));
            psi.WorkingDirectory = tmp; psi.UseShellExecute = false; psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true; psi.RedirectStandardError = true;
            using (var p = Process.Start(psi)) { p.StandardOutput.ReadToEnd(); p.StandardError.ReadToEnd(); p.WaitForExit(); }
            progress("Decoding audio " + Math.Min(i + 60, wems.Count) + "/" + wems.Count);
        }
        foreach (string wav in Directory.GetFiles(tmp, "*.wav")) {
            string n = Path.GetFileNameWithoutExtension(wav);
            if (n.EndsWith(".wem")) n = n.Substring(0, n.Length - 4);
            string dst = n.StartsWith("v_") ? Path.Combine(Dir, "voice", n.Substring(2) + ".wav") : Path.Combine(Dir, "sfx", n.Substring(2) + ".wav");
            if (File.Exists(dst)) File.Delete(dst);
            File.Move(wav, dst);
        }
        Directory.Delete(tmp, true);
        if (Directory.GetFiles(Path.Combine(Dir, "voice"), "*.wav").Length == 0) throw new Exception("no voice lines could be decoded");
        File.WriteAllText(Path.Combine(Dir, "ready.txt"), Version);
        progress("Ready");
    }
}

// Embedded media of one Wwise sound bank (DIDX index + DATA blob).
class Bank {
    readonly Dictionary<uint, long[]> index = new Dictionary<uint, long[]>();
    byte[] data;

    public static Bank Load(string paks, string name) {
        string member = "sounds/desktop/" + name + ".bnk";
        foreach (string f in Directory.GetFiles(paks, "default_sound*.pak"))
            using (var pak = new Pak(f))
                if (pak.Has(member)) return new Bank(pak.Read(member));
        throw new Exception("sound bank " + name + " not found");
    }

    Bank(byte[] b) {
        int p = 0;
        while (p + 8 <= b.Length) {
            string tag = System.Text.Encoding.ASCII.GetString(b, p, 4);
            int size = BitConverter.ToInt32(b, p + 4);
            if (tag == "DIDX")
                for (int o = 0; o + 12 <= size; o += 12)
                    index[BitConverter.ToUInt32(b, p + 8 + o)] = new long[] { BitConverter.ToUInt32(b, p + 12 + o), BitConverter.ToUInt32(b, p + 16 + o) };
            else if (tag == "DATA") { data = new byte[size]; Buffer.BlockCopy(b, p + 8, data, 0, size); }
            p += 8 + size;
        }
    }

    public byte[] Media(uint id) {
        long[] e;
        if (data == null || !index.TryGetValue(id, out e)) return null;
        byte[] r = new byte[e[1]];
        Buffer.BlockCopy(data, (int)e[0], r, 0, (int)e[1]);
        return r;
    }
}
