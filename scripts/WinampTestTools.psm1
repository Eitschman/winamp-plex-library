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
  [StructLayout(LayoutKind.Sequential)] struct CopyData {
    public IntPtr Tag;
    public int Size;
    public IntPtr Data;
  }
  public static void EnqueuePlaylist(IntPtr window, string path) {
    if (!System.IO.Path.IsPathRooted(path) || !path.EndsWith(".m3u8", StringComparison.OrdinalIgnoreCase))
      throw new ArgumentException("Absolute M3U8 path required");
    IntPtr name = Marshal.StringToHGlobalUni(path);
    IntPtr payload = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyData)));
    try {
      Marshal.StructureToPtr(new CopyData { Tag = (IntPtr)1100, Size = (path.Length + 1) * 2, Data = name }, payload, false);
      Query(window, 74, IntPtr.Zero, payload);
    } finally { Marshal.FreeHGlobal(payload); Marshal.FreeHGlobal(name); }
  }
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
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int process);
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint operation);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr read);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  public static string CatalogCellText(IntPtr window, int processId, int row, int column) {
    uint actual; GetWindowThreadProcessId(window, out actual);
    if(actual != processId || row < 0 || row >= QueryInt32(window,4100,IntPtr.Zero,IntPtr.Zero) || column < 0 || column > 7)
      throw new ArgumentException("Invalid test catalog cell");
    IntPtr process = OpenProcess(0x38,false,processId), remote = IntPtr.Zero;
    if(process == IntPtr.Zero) throw new InvalidOperationException("Test process memory unavailable");
    try {
      remote = VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)8192,0x3000,4);
      if(remote == IntPtr.Zero || (ulong)remote.ToInt64() > uint.MaxValue) throw new InvalidOperationException("Test requires x86 catalog");
      byte[] item = new byte[64], text = new byte[4096];
      Buffer.BlockCopy(BitConverter.GetBytes(column),0,item,8,4);
      Buffer.BlockCopy(BitConverter.GetBytes((uint)remote.ToInt64()+64),0,item,20,4);
      Buffer.BlockCopy(BitConverter.GetBytes(2048),0,item,24,4);
      UIntPtr transferred;
      if(!WriteProcessMemory(process,remote,item,(UIntPtr)item.Length,out transferred) || transferred.ToUInt64() != (ulong)item.Length)
        throw new InvalidOperationException("Test catalog request unavailable");
      int length = QueryInt32(window,4211,(IntPtr)row,remote);
      if(length < 0 || length >= 2048 || !ReadProcessMemory(process,IntPtr.Add(remote,64),text,(UIntPtr)text.Length,out transferred) || transferred.ToUInt64() != (ulong)text.Length)
        throw new InvalidOperationException("Test catalog text unavailable");
      return Encoding.Unicode.GetString(text,0,length*2);
    } finally { if(remote != IntPtr.Zero) VirtualFreeEx(process,remote,UIntPtr.Zero,0x8000); CloseHandle(process); }
  }
  public static IntPtr Query(IntPtr window, uint message, IntPtr first, IntPtr second) {
    IntPtr result;
    if(window==IntPtr.Zero || SendMessageTimeout(window,message,first,second,2,3000,out result)==IntPtr.Zero)
      throw new InvalidOperationException("Winamp IPC timeout or invalid window");
    return result;
  }
  sealed class CatalogMemory : IDisposable {
    public IntPtr Process, Address;
    readonly int capacity;
    public CatalogMemory(IntPtr window, int processId, int size = 256) {
      if(size < 16 || size > 8192) throw new ArgumentException("Invalid test memory size");
      capacity = size;
      uint owner; GetWindowThreadProcessId(window,out owner);
      if(owner != processId) throw new InvalidOperationException("Foreign catalog window");
      Process=OpenProcess(0x38,false,processId);
      if(Process==IntPtr.Zero) throw new InvalidOperationException("Catalog process unavailable");
      Address=VirtualAllocEx(Process,IntPtr.Zero,(UIntPtr)capacity,0x3000,4);
      if(Address==IntPtr.Zero || (ulong)Address.ToInt64()>uint.MaxValue) { Dispose(); throw new InvalidOperationException("x86 catalog memory unavailable"); }
    }
    public void Write(byte[] bytes) {
      UIntPtr written;
      if(bytes.Length>capacity || !WriteProcessMemory(Process,Address,bytes,(UIntPtr)bytes.Length,out written) || written.ToUInt64()!=(ulong)bytes.Length)
        throw new InvalidOperationException("Catalog request write failed");
    }
    public byte[] Read(int length) {
      byte[] bytes=new byte[length]; UIntPtr read;
      if(length>capacity || !ReadProcessMemory(Process,Address,bytes,(UIntPtr)length,out read) || read.ToUInt64()!=(ulong)length)
        throw new InvalidOperationException("Catalog response read failed");
      return bytes;
    }
    public void Dispose() {
      if(Address!=IntPtr.Zero) { VirtualFreeEx(Process,Address,UIntPtr.Zero,0x8000); Address=IntPtr.Zero; }
      if(Process!=IntPtr.Zero) { CloseHandle(Process); Process=IntPtr.Zero; }
    }
  }
  // SDK 5.55: extendedFileInfoStructW enthält unter x86 vier 32-Bit-Felder.
  public static string ExtendedMetadata(IntPtr window, int processId, string filename, string field) {
    if(String.IsNullOrEmpty(filename) || filename.Length > 1023 ||
       (field != "artist" && field != "albumartist" && field != "title" && field != "album"))
      throw new ArgumentException("Invalid metadata test request");
    using(var memory = new CatalogMemory(window, processId, 8192)) {
      byte[] request = new byte[8192];
      uint address = (uint)memory.Address.ToInt64();
      Buffer.BlockCopy(BitConverter.GetBytes(address + 32),0,request,0,4);
      Buffer.BlockCopy(BitConverter.GetBytes(address + 2080),0,request,4,4);
      Buffer.BlockCopy(BitConverter.GetBytes(address + 2208),0,request,8,4);
      Buffer.BlockCopy(BitConverter.GetBytes(2048),0,request,12,4);
      byte[] name = Encoding.Unicode.GetBytes(filename + "\0"), key = Encoding.Unicode.GetBytes(field + "\0");
      Buffer.BlockCopy(name,0,request,32,name.Length);
      Buffer.BlockCopy(key,0,request,2080,key.Length);
      memory.Write(request);
      // IPC_GET_EXTENDED_FILE_INFOW_HOOKABLE bezieht den Media-Library-Hook ein.
      if(QueryInt32(window,1024,memory.Address,(IntPtr)3027) != 1)
        throw new InvalidOperationException("Winamp metadata query rejected");
      string result = Encoding.Unicode.GetString(memory.Read(8192),2208,4096);
      int end = result.IndexOf('\0');
      if(end < 0) throw new InvalidOperationException("Metadata response not terminated");
      return result.Substring(0,end);
    }
  }
  public static void SelectCatalogRows(IntPtr window, int processId, int[] rows) {
    int count=QueryInt32(window,4100,IntPtr.Zero,IntPtr.Zero);
    if(rows.Length==0) throw new ArgumentException("Empty test selection");
    foreach(int row in rows) if(row<0 || row>=count) throw new ArgumentException("Invalid test selection");
    using(var memory=new CatalogMemory(window,processId)) {
      byte[] item=new byte[64]; Buffer.BlockCopy(BitConverter.GetBytes(3),0,item,16,4);
      memory.Write(item);
      if(QueryInt32(window,4139,(IntPtr)(-1),memory.Address)==0) throw new InvalidOperationException("Catalog selection clear failed");
      foreach(int row in rows) {
        Buffer.BlockCopy(BitConverter.GetBytes(row==rows[0]?3:2),0,item,12,4); memory.Write(item);
        if(QueryInt32(window,4139,(IntPtr)row,memory.Address)==0) throw new InvalidOperationException("Catalog selection failed");
      }
    }
  }
  public static string TreeItemText(IntPtr window, int processId, IntPtr node) {
    if(node==IntPtr.Zero || node.ToInt64()<0 || (ulong)node.ToInt64()>uint.MaxValue) throw new ArgumentException("Invalid tree item");
    using(var memory=new CatalogMemory(window,processId)) {
      byte[] item=new byte[256]; Buffer.BlockCopy(BitConverter.GetBytes(1),0,item,0,4);
      Buffer.BlockCopy(BitConverter.GetBytes((uint)node.ToInt64()),0,item,4,4);
      Buffer.BlockCopy(BitConverter.GetBytes((uint)memory.Address.ToInt64()+64),0,item,16,4);
      Buffer.BlockCopy(BitConverter.GetBytes(96),0,item,20,4); memory.Write(item);
      if(QueryInt32(window,4414,IntPtr.Zero,memory.Address)==0) throw new InvalidOperationException("Tree text query failed");
      return Encoding.Unicode.GetString(memory.Read(256),64,192).TrimEnd('\0');
    }
  }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint GetGuiResources(IntPtr process, uint flags);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetProcessHandleCount(IntPtr process, out uint count);
  public static int CatalogImageIndex(IntPtr window, int processId, int row) {
    if(row<0 || row>=QueryInt32(window,4100,IntPtr.Zero,IntPtr.Zero)) throw new ArgumentException("Invalid image row");
    using(var memory=new CatalogMemory(window,processId)) {
      byte[] item=new byte[64]; Buffer.BlockCopy(BitConverter.GetBytes(2),0,item,0,4); Buffer.BlockCopy(BitConverter.GetBytes(row),0,item,4,4);
      memory.Write(item);
      if(QueryInt32(window,4171,IntPtr.Zero,memory.Address)==0) throw new InvalidOperationException("Catalog image query failed");
      return BitConverter.ToInt32(memory.Read(64),28);
    }
  }
  public static void ClickCatalogHeader(IntPtr window, int processId, int column) {
    IntPtr header=Query(window,4127,IntPtr.Zero,IntPtr.Zero);
    if(column<0 || column>=QueryInt32(header,4608,IntPtr.Zero,IntPtr.Zero)) throw new ArgumentException("Invalid sort column");
    using(var memory=new CatalogMemory(window,processId)) {
      memory.Write(new byte[16]);
      if(QueryInt32(header,4615,(IntPtr)column,memory.Address)==0) throw new InvalidOperationException("Header rectangle unavailable");
      byte[] rectangle=memory.Read(16);
      int x=(BitConverter.ToInt32(rectangle,0)+BitConverter.ToInt32(rectangle,8))/2;
      int y=(BitConverter.ToInt32(rectangle,4)+BitConverter.ToInt32(rectangle,12))/2;
      if(x<0 || x>32767 || y<0 || y>32767) throw new InvalidOperationException("Header outside test viewport");
      IntPtr point=(IntPtr)(x | (y<<16));
      Query(header,513,(IntPtr)1,point); Query(header,514,IntPtr.Zero,point);
    }
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
    return $Location -cmatch ('^' + [regex]::Escape($directory) + '\\session-[0-9a-f]{48}\\[0-9a-f]{48}\.(flac|ogg|m4a)$')
}

# Eine Freigabeverletzung belegt den noch exklusiv geöffneten Medien-Schreibhandle.
# Fehlende Dateien und Zugriffsfehler dürfen diesen Nachweis nicht vortäuschen.
function Test-PlexMediaWriteLock([string]$Path) {
    try {
        $reader = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader.Dispose()
        return $false
    } catch [IO.IOException] {
        if (($_.Exception.HResult -band 65535) -ne 32) { throw }
        return $true
    }
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

function Select-PlexFeedbackTrack($Sample) {
    $process = Get-PlexTestProcess
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq 'SysTreeView32').Count -gt 0
    })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
    $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
    # Ein neu erzeugter Titelknoten verhindert geerbte Suche oder Sortierung anderer Tests.
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $root)
    $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
    for ($step = 0; $step -lt 2; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) }
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
    Start-Sleep -Milliseconds 200
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $view = [PlexNative]::GetParent($list.Handle)
    # Bei bereits aktiver Titelansicht kann noch eine Meldung des vorigen Tests stehen.
    [void][PlexNative]::Query($view, 273, [IntPtr]1054, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^\d+ \.\.\. \d+') { break }
        if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Titelansicht meldet einen Fehler.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($status.Text -notmatch '^1 \.\.\. \d+') { throw 'Erste Titelseite wurde nicht geladen.' }
    # Vor jeder Änderung den konkreten Titel anhand aller drei sichtbaren Metadaten prüfen.
    foreach ($column in 0..2) {
        $expected = @($Sample.title, $Sample.artist, $Sample.album)[$column]
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, $Sample.row_index, $column) -cne $expected) {
            throw 'API-Testtitel und sichtbare Tabellenzeile stimmen nicht überein.'
        }
    }
    [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]36, [IntPtr]::Zero)
    for ($row = 0; $row -lt $Sample.row_index; ++$row) { [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]40, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 150
    if ([PlexNative]::QueryInt32($list.Handle, 4108, [IntPtr](-1), [IntPtr]2) -ne $Sample.row_index -or
        [PlexNative]::QueryInt32($list.Handle, 4146, [IntPtr]::Zero, [IntPtr]::Zero) -ne 1) { throw 'Testtitel ist nicht einzeln ausgewählt.' }
    return [pscustomobject]@{ List = $list.Handle; View = $view; Outer = $outer[0].Handle }
}

Export-ModuleMember -Function Get-PlexTestProcess, Save-PlexWindowScreenshot, Test-PlexPlaybackLocation, Test-PlexMediaWriteLock, Select-PlexFeedbackTrack
