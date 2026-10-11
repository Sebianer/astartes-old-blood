# Sends key presses (scan codes, so DirectInput games see them) to Old Blood after bringing it to the front.
# Usage: keys.ps1 ENTER,ENTER,W:1500  (NAME[:holdMs]; names: ENTER ESC UP DOWN LEFT RIGHT SPACE W A S D V F E TAB or WAIT:ms)
param([string]$seq, [string]$proc = "WolfOldBlood_x64")
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Threading;
public class K {
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  public static void Press(byte vk, byte scan, bool ext, int hold) {
    uint f = 0x0008 | (ext ? 1u : 0u); keybd_event(vk, scan, f, UIntPtr.Zero); Thread.Sleep(hold); keybd_event(vk, scan, f | 2, UIntPtr.Zero); }
}
"@
$map = @{ ENTER=@(0x0D,0x1C,0); ESC=@(0x1B,0x01,0); UP=@(0x26,0x48,1); DOWN=@(0x28,0x50,1); LEFT=@(0x25,0x4B,1); RIGHT=@(0x27,0x4D,1);
  SPACE=@(0x20,0x39,0); W=@(0x57,0x11,0); A=@(0x41,0x1E,0); S=@(0x53,0x1F,0); D=@(0x44,0x20,0); V=@(0x56,0x2F,0); F=@(0x46,0x21,0); E=@(0x45,0x12,0); TAB=@(0x09,0x0F,0) }
$p = Get-Process $proc | Select-Object -First 1
[K]::ShowWindow($p.MainWindowHandle, 9) | Out-Null; [K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 500
foreach ($item in $seq.Split(',')) {
  $parts = $item.Split(':'); $name = $parts[0]; $hold = 80; if ($parts.Length -gt 1) { $hold = [int]$parts[1] }
  if ($name -eq 'WAIT') { Start-Sleep -Milliseconds $hold; continue }
  $k = $map[$name]; [K]::Press([byte]$k[0], [byte]$k[1], [bool]$k[2], $hold); Start-Sleep -Milliseconds 400
}
