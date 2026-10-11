// Astartes: The Old Blood launcher: pick a Space Marine 2 class, then Old Blood starts with it.
// Usage: AstartesLauncher.exe --sm2 "<Space Marine 2 folder>"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Media;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

static class Program {
    public static readonly string SettingsPath = Path.Combine(Content.Root, "launcher.ini");
    public static readonly string SessionPath = Path.Combine(Content.Root, "session.ini");

    [STAThread]
    public static void Log(string s) {
        try { File.AppendAllText(Path.Combine(Content.Root, "launcher.log"), DateTime.Now.ToString("HH:mm:ss.fff ") + s + Environment.NewLine); } catch { }
    }

    static int Main(string[] args) {
        Directory.CreateDirectory(Content.Root);
        Log("start pid " + Process.GetCurrentProcess().Id + " args: " + string.Join(" ", args));
        AppDomain.CurrentDomain.UnhandledException += (s, e) => Log("crash: " + e.ExceptionObject);
        var ini = Ini.Load(SettingsPath);
        for (int i = 0; i + 1 < args.Length; i++) if (args[i] == "--sm2") ini["sm2"] = args[i + 1];
        bool selftest = args.Contains("--extract-only");
        Ini.Save(SettingsPath, ini);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (!Content.Ready() || selftest) {
            string sm2 = ini.ContainsKey("sm2") ? ini["sm2"] : null;
            if (sm2 == null || !Directory.Exists(sm2)) {
                MessageBox.Show("Space Marine 2 was not found. Astartes: The Old Blood reads its class art and voices from your own copy of Space Marine 2.", "Astartes: The Old Blood");
                return 1;
            }
            if (!SetupForm.Run(sm2)) return 1;
            if (selftest) return 0;
        }
        string test = MeltyTestFile();
        Log("melty test file: " + (test ?? "none"));
        if (test != null && StartTest(test)) { WaitForGame(); return 0; }
        Log("showing picker");
        var picker = new PickerForm(ini); // keeps watching for a test file that shows up late
        Application.Run(picker);
        if (picker.Started) WaitForGame();
        return 0;
    }

    // Stay running while Old Blood runs, so whoever started us (Melty) sees one process for the whole session.
    static void WaitForGame() {
        Process game = null;
        for (int i = 0; i < 120 && game == null; i++) { game = Process.GetProcessesByName("WolfOldBlood_x64").FirstOrDefault(); if (game == null) Thread.Sleep(500); }
        if (game == null) { Log("game never appeared"); return; }
        Log("game running, pid " + game.Id);
        game.WaitForExit();
        Log("game exited");
    }

    // Melty test run: no picker, one class per run, and tell the hook where to report checks.
    public static bool StartTest(string test) {
        string json = null;
        for (int i = 0; i < 20 && json == null; i++) { // Melty may still be writing it
            try { json = File.ReadAllText(test); } catch (IOException) { Thread.Sleep(250); }
        }
        if (json == null) return false;
        int run = 1;
        var m = System.Text.RegularExpressions.Regex.Match(json, "\"run\"\\s*:\\s*(\\d+)");
        if (m.Success) run = int.Parse(m.Groups[1].Value);
        var c = GenData.Classes[(Math.Max(run, 1) - 1) % GenData.Classes.Length];
        return PickerForm.StartGame(c.Id, true, true, test);
    }

    // %LOCALAPPDATA%\Melty\test\<slug>.json exists only while Melty test-runs a release.
    public static string MeltyTestFile() {
        string d = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Melty\test");
        if (!Directory.Exists(d)) return null;
        return Directory.GetFiles(d, "*.json").OrderByDescending(File.GetLastWriteTimeUtc).FirstOrDefault();
    }
}

static class Ini {
    public static Dictionary<string, string> Load(string path) {
        var d = new Dictionary<string, string>();
        if (File.Exists(path))
            foreach (string l in File.ReadAllLines(path)) { int e = l.IndexOf('='); if (e > 0) d[l.Substring(0, e).Trim()] = l.Substring(e + 1).Trim(); }
        return d;
    }
    public static void Save(string path, Dictionary<string, string> d, string header = null) {
        var lines = new List<string>();
        if (header != null) lines.Add(header);
        lines.AddRange(d.Select(kv => kv.Key + "=" + kv.Value));
        File.WriteAllLines(path, lines);
    }
}

class SetupForm : Form {
    readonly Label status = new Label();
    Exception error;

    public static bool Run(string sm2) {
        var f = new SetupForm();
        var t = new Thread(() => {
            try { Content.Extract(sm2, s => f.BeginInvoke((Action)(() => f.status.Text = s))); }
            catch (Exception e) { f.error = e; }
            f.BeginInvoke((Action)f.Close);
        });
        f.Shown += (s, e) => t.Start();
        Application.Run(f);
        if (f.error != null) { MessageBox.Show("Setup failed: " + f.error.Message, "Astartes: The Old Blood"); return false; }
        return true;
    }

    SetupForm() {
        Text = "Astartes: The Old Blood"; ClientSize = new Size(520, 120); FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen; BackColor = Color.FromArgb(16, 16, 18); ForeColor = Color.Gainsboro; MaximizeBox = false;
        var title = new Label { Text = "Preparing your Space Marine 2 armoury (first start only)", Font = new Font("Segoe UI", 11f, FontStyle.Bold), Location = new Point(20, 22), AutoSize = true };
        status.Location = new Point(20, 62); status.AutoSize = true; status.Font = new Font("Segoe UI", 10f); status.Text = "Starting";
        Controls.Add(title); Controls.Add(status);
    }
}

class PickerForm : Form {
    static readonly Color Gold = Color.FromArgb(214, 172, 74), Bg = Color.FromArgb(14, 14, 16), Panel = Color.FromArgb(28, 28, 32);
    const int CardW = 176, CardH = 280, Gap = 14;
    readonly Dictionary<string, string> ini;
    readonly List<PictureBox> cards = new List<PictureBox>();
    readonly Label detail = new Label(), abilityLbl = new Label();
    readonly CheckBox skipIntro = new CheckBox(), deployMoment = new CheckBox();
    readonly Random rng = new Random();
    int selected, hovered = -1;
    SoundPlayer player;
    public bool Started;

    public PickerForm(Dictionary<string, string> ini) {
        this.ini = ini;
        Text = "Astartes: The Old Blood"; BackColor = Bg; ForeColor = Color.Gainsboro; DoubleBuffered = true;
        FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen;
        int n = GenData.Classes.Length;
        ClientSize = new Size(Gap + n * (CardW + Gap), 560);
        string last = ini.ContainsKey("class") ? ini["class"] : GenData.Classes[0].Id;
        selected = Math.Max(0, Array.FindIndex(GenData.Classes, c => c.Id == last));

        Controls.Add(new Label { Text = "CHOOSE YOUR CLASS", Font = new Font("Segoe UI", 18f, FontStyle.Bold), ForeColor = Gold, Location = new Point(Gap, 12), AutoSize = true });
        Controls.Add(new Label { Text = "Battle-brother, Wolfenstein needs the Emperor's finest.", Font = new Font("Segoe UI", 10f, FontStyle.Italic), ForeColor = Color.Silver, Location = new Point(Gap + 2, 48), AutoSize = true });

        for (int i = 0; i < n; i++) {
            var c = GenData.Classes[i];
            int idx = i;
            var pb = new PictureBox { Location = new Point(Gap + i * (CardW + Gap), 80), Size = new Size(CardW, CardH), SizeMode = PictureBoxSizeMode.Zoom, BackColor = Panel, Cursor = Cursors.Hand };
            pb.Image = LoadCard(c.Id);
            pb.Paint += (s, e) => PaintCard(e.Graphics, idx);
            pb.MouseEnter += (s, e) => { hovered = idx; PlayHover(idx); Refresh(); };
            pb.MouseLeave += (s, e) => { if (hovered == idx) hovered = -1; Refresh(); };
            pb.Click += (s, e) => { selected = idx; UpdateDetail(); Refresh(); };
            pb.DoubleClick += (s, e) => { selected = idx; Deploy(); };
            cards.Add(pb); Controls.Add(pb);
            Controls.Add(new Label { Text = c.Name.ToUpper(), Font = new Font("Segoe UI", 11f, FontStyle.Bold), ForeColor = Color.White, Location = new Point(pb.Left, pb.Bottom + 6), Size = new Size(CardW, 22), TextAlign = ContentAlignment.MiddleCenter });
        }

        detail.Location = new Point(Gap, 400); detail.Size = new Size(ClientSize.Width - 2 * Gap, 24); detail.Font = new Font("Segoe UI", 10.5f);
        abilityLbl.Location = new Point(Gap, 426); abilityLbl.Size = new Size(ClientSize.Width - 2 * Gap, 24); abilityLbl.Font = new Font("Segoe UI", 10.5f, FontStyle.Bold); abilityLbl.ForeColor = Gold;
        Controls.Add(detail); Controls.Add(abilityLbl);

        skipIntro.Text = "Skip intro videos"; skipIntro.Checked = !ini.ContainsKey("skipIntro") || ini["skipIntro"] == "1";
        deployMoment.Text = "Deployment moment at each level start (battle cry, HUD power-on, armour full)";
        deployMoment.Checked = !ini.ContainsKey("deployMoment") || ini["deployMoment"] == "1";
        foreach (var cb in new[] { skipIntro, deployMoment }) { cb.AutoSize = true; cb.Font = new Font("Segoe UI", 10f); cb.ForeColor = Color.Gainsboro; Controls.Add(cb); }
        skipIntro.Location = new Point(Gap, 470); deployMoment.Location = new Point(Gap, 496);

        var go = new Button { Text = "DEPLOY", Font = new Font("Segoe UI", 14f, FontStyle.Bold), Size = new Size(200, 52), FlatStyle = FlatStyle.Flat, BackColor = Gold, ForeColor = Color.Black, Cursor = Cursors.Hand };
        go.FlatAppearance.BorderSize = 0;
        go.Location = new Point(ClientSize.Width - Gap - go.Width, 474);
        go.Click += (s, e) => Deploy();
        Controls.Add(go); AcceptButton = go;
        Controls.Add(new Label { Text = "Ability key: " + GenData.AbilityKey + "  ·  Art and voices are read from your own Space Marine 2 install.", Font = new Font("Segoe UI", 8.5f), ForeColor = Color.Gray, Location = new Point(Gap, 532), AutoSize = true });
        UpdateDetail();
        // Melty can start us a moment before it writes its test file: switch to test mode when it appears
        var poll = new System.Windows.Forms.Timer { Interval = 500 };
        poll.Tick += (s, e) => { string t = Program.MeltyTestFile(); if (t != null) { poll.Stop(); if (Program.StartTest(t)) { Started = true; Close(); } } };
        poll.Start();
    }

    static Image LoadCard(string id) {
        string f = Content.Card(id);
        if (!File.Exists(f)) return null;
        byte[] px = File.ReadAllBytes(f);
        var bmp = new Bitmap(Content.CardW, Content.CardH, PixelFormat.Format32bppArgb);
        var bd = bmp.LockBits(new Rectangle(0, 0, Content.CardW, Content.CardH), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        for (int y = 0; y < Content.CardH; y++) Marshal.Copy(px, y * Content.CardW * 4, bd.Scan0 + y * bd.Stride, Content.CardW * 4);
        bmp.UnlockBits(bd);
        return bmp;
    }

    void PaintCard(Graphics g, int i) {
        if (i == selected) using (var p = new Pen(Gold, 5)) g.DrawRectangle(p, 2, 2, CardW - 5, CardH - 5);
        else if (i == hovered) using (var p = new Pen(Color.FromArgb(160, Gold), 2)) g.DrawRectangle(p, 1, 1, CardW - 3, CardH - 3);
        else using (var b = new SolidBrush(Color.FromArgb(70, 0, 0, 0))) g.FillRectangle(b, 0, 0, CardW, CardH);
    }

    void UpdateDetail() {
        var c = GenData.Classes[selected];
        detail.Text = string.Format("{0}  ·  Speed {1:0}%  ·  Jump {2:0}%  ·  Damage taken {3:0}%  ·  Damage dealt {4:0}%",
            c.Name, c.Sprint / 450f * 100, c.Jump / 62f * 100, c.Taken * 100, c.Dealt * 100);
        abilityLbl.Text = "Ability [" + GenData.AbilityKey + "]: " + c.AbilityName + " — " + c.AbilityText;
    }

    void PlayHover(int i) {
        var takes = Content.VoiceTakes(GenData.Classes[i].Id, "hover");
        if (takes.Length == 0) return;
        try {
            if (player != null) player.Stop();
            player = new SoundPlayer(takes[rng.Next(takes.Length)]);
            player.Play();
        } catch { }
    }

    void Deploy() {
        var c = GenData.Classes[selected];
        ini["class"] = c.Id; ini["skipIntro"] = skipIntro.Checked ? "1" : "0"; ini["deployMoment"] = deployMoment.Checked ? "1" : "0";
        Ini.Save(Program.SettingsPath, ini);
        if (StartGame(c.Id, skipIntro.Checked, deployMoment.Checked, null)) { Started = true; Close(); }
    }

    // Writes the session the hook reads at game start, then asks Steam to start Old Blood.
    public static bool StartGame(string classId, bool skip, bool deploy, string testFile) {
        var session = new Dictionary<string, string> {
            { "class", classId }, { "skipIntro", skip ? "1" : "0" }, { "deployMoment", deploy ? "1" : "0" },
            { "content", Content.Dir }, { "written", DateTime.UtcNow.ToString("o") } };
        if (testFile != null) session["testFile"] = testFile;
        Ini.Save(Program.SessionPath, session, "[session]");
        Program.Log("starting game as " + classId + (testFile != null ? " (melty test)" : ""));
        try { Process.Start("steam://rungameid/350080"); return true; }
        catch (Exception e) { Program.Log("steam start failed: " + e.Message); MessageBox.Show("Could not ask Steam to start Wolfenstein: The Old Blood: " + e.Message, "Astartes: The Old Blood"); return false; }
    }
}
