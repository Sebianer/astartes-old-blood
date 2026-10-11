# Screenshot of a process's main window (default: Old Blood) to the given png path.
param([string]$out, [string]$proc = "WolfOldBlood_x64")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class ShotW { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
public struct RECT { public int L, T, R, B; } }
"@
$p = Get-Process $proc | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[ShotW]::ShowWindow($p.MainWindowHandle, 9) | Out-Null; [ShotW]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 700
$r = New-Object ShotW+RECT; [ShotW]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($out); "$($r.R - $r.L)x$($r.B - $r.T)"
