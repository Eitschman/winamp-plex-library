$ErrorActionPreference = 'Stop'
$script:ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class PlexNativeWindow {
  public IntPtr Handle;
  public int ProcessId;
  public int ControlId;
  public string Class;
  public string Text;
}
public static class PlexNative {
  public delegate bool EnumProc(IntPtr window, IntPtr parameter);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr parameter);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int maximum);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int maximum);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr first, string second);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr first, IntPtr second, uint flags, uint timeout, out IntPtr result);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr first, StringBuilder second, uint flags, uint timeout, out IntPtr result);
  public static string ControlText(IntPtr window) {
    var buffer = new StringBuilder(2049); IntPtr result;
    if(SendMessageTimeout(window,13,(IntPtr)buffer.Capacity,buffer,2,3000,out result)==IntPtr.Zero)
      throw new InvalidOperationException("Control text timeout");
    return buffer.ToString();
  }
  public static string ListItemText(IntPtr window, int index) {
    int length = QueryInt32(window,394,(IntPtr)index,IntPtr.Zero);
    if(length < 0 || length > 2048) throw new InvalidOperationException("Invalid list text length");
    var buffer = new StringBuilder(length+1); IntPtr result;
    if(SendMessageTimeout(window,393,(IntPtr)index,buffer,2,3000,out result)==IntPtr.Zero)
      throw new InvalidOperationException("List text timeout");
    return buffer.ToString();
  }
  public static IntPtr Query(IntPtr window, uint message, IntPtr first, IntPtr second) {
    IntPtr result;
    if(window==IntPtr.Zero || SendMessageTimeout(window,message,first,second,2,3000,out result)==IntPtr.Zero)
      throw new InvalidOperationException("Winamp IPC timeout or invalid window");
    return result;
  }
  public static int QueryInt32(IntPtr window, uint message, IntPtr first, IntPtr second) {
    // Winamp x86 kann negative IPC-Werte als 0xFFFFFFFF an den 64-Bit-Testprozess liefern.
    return unchecked((int)Query(window,message,first,second).ToInt64());
  }
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll", EntryPoint="GetWindowLongW")] public static extern int WindowStyle(IntPtr window, int index);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int left, int top, int width, int height, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int left, int top, int width, int height, uint flags);
  [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr window, IntPtr rectangle, IntPtr region, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] struct GuiThreadInfo {
    public int Size; public uint Flags;
    public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
    public Rect CaretRectangle;
  }
  [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo information);
  public static int FocusedControl(IntPtr window) {
    uint owner; var thread=GetWindowThreadProcessId(window,out owner);
    var information = new GuiThreadInfo { Size=Marshal.SizeOf(typeof(GuiThreadInfo)) };
    if(!GetGUIThreadInfo(thread,ref information)) throw new InvalidOperationException("GUI focus unavailable");
    return GetDlgCtrlID(information.Focus);
  }
  public static IntPtr FocusedWindow(IntPtr window) {
    uint owner; var thread=GetWindowThreadProcessId(window,out owner);
    var information = new GuiThreadInfo { Size=Marshal.SizeOf(typeof(GuiThreadInfo)) };
    if(!GetGUIThreadInfo(thread,ref information)) throw new InvalidOperationException("GUI focus unavailable");
    return information.Focus;
  }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
  [DllImport("user32.dll")] static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
  public static void ClickTestMenu(IntPtr menu, int process, int x, int y) {
    uint owner; GetWindowThreadProcessId(menu, out owner);
    Rect rectangle;
    if(owner!=process || !GetWindowRect(menu,out rectangle)) throw new InvalidOperationException("Invalid test menu");
    var point = new Point { X=rectangle.Left+x, Y=rectangle.Top+y };
    if(point.X>=rectangle.Right || point.Y>=rectangle.Bottom || !SetCursorPos(point.X,point.Y))
      throw new InvalidOperationException("Invalid menu position");
    // Nur das weiterhin sichtbare Menü der isolierten Testinstanz darf einen Klick erhalten.
    if(WindowFromPoint(point)!=menu) throw new InvalidOperationException("Test menu is obscured");
    mouse_event(2,0,0,0,UIntPtr.Zero);
    mouse_event(4,0,0,0,UIntPtr.Zero);
  }
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr device, uint flags);
  static PlexNativeWindow Read(IntPtr window) {
    uint process; GetWindowThreadProcessId(window, out process);
    var text = new StringBuilder(1024); GetWindowText(window, text, text.Capacity);
    var type = new StringBuilder(256); GetClassName(window, type, type.Capacity);
    return new PlexNativeWindow { Handle=window, ProcessId=(int)process, ControlId=GetDlgCtrlID(window), Class=type.ToString(), Text=text.ToString() };
  }
  public static PlexNativeWindow[] Windows(int process) {
    var result = new List<PlexNativeWindow>();
    EnumWindows((window, parameter) => { var item=Read(window); if(item.ProcessId==process) result.Add(item); return true; }, IntPtr.Zero);
    return result.ToArray();
  }
  public static PlexNativeWindow[] Children(IntPtr parent) {
    var result = new List<PlexNativeWindow>();
    EnumChildWindows(parent, (window, parameter) => { result.Add(Read(window)); return true; }, IntPtr.Zero);
    return result.ToArray();
  }
}
'@

function Get-PlexTestProcess {
    $expected = Join-Path $script:ProjectRoot '.local\winamp-test\winamp.exe'
    $process = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })
    if ($process.Count -ne 1) { throw 'Genau eine isolierte Winamp-Testinstanz wird erwartet.' }
    return $process[0]
}

function Test-PlexPlaybackLocation([string]$Location) {
    if ($Location -match '^http://127\.0\.0\.1:[0-9]+/stream/[0-9a-f]{48}/audio\.(mp3|aac|m4a)$') { return $true }
    $directory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'WinampPlexLibrary\playback'
    return $Location -cmatch ('^' + [regex]::Escape($directory) + '\\session-[0-9a-f]{48}\\[0-9a-f]{48}\.(flac|ogg)$')
}

function Save-PlexWindowScreenshot([IntPtr]$Window, [string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $rectangle = New-Object PlexNative+Rect
    if (-not [PlexNative]::GetWindowRect($Window, [ref]$rectangle)) { throw 'Fensterrechteck nicht verfügbar.' }
    $bitmap = New-Object Drawing.Bitmap(($rectangle.Right - $rectangle.Left), ($rectangle.Bottom - $rectangle.Top))
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $device = $graphics.GetHdc()
    try {
        if (-not [PlexNative]::PrintWindow($Window, $device, 0)) { throw 'Fenster konnte nicht aufgenommen werden.' }
    } finally { $graphics.ReleaseHdc($device); $graphics.Dispose() }
    try { $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}

Export-ModuleMember -Function Get-PlexTestProcess, Save-PlexWindowScreenshot, Test-PlexPlaybackLocation
