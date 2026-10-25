# Run once in an interactive 64-bit Windows session, with a fresh ReportRoot.
# Never Apply/OK after opening Formats: it writes per-user shell registration.
param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][string]$SourceProfile,
 [Parameter(Mandatory=$true)][string]$RussianLanguageDll,
 [Parameter(Mandatory=$true)][string]$ReportRoot
)
$ErrorActionPreference='Stop'
if(-not [Environment]::Is64BitProcess){throw 'Use 64-bit PowerShell for the x64 player'}
if(Test-Path $ReportRoot){throw 'ReportRoot must be a new directory'}
$base=$ReportRoot
$dir="$base\player"
New-Item -ItemType Directory -Force "$dir\Lang" | Out-Null
Add-Type -AssemblyName System.Drawing,UIAutomationClient,UIAutomationTypes
Add-Type @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class RestoreNative {
 [StructLayout(LayoutKind.Sequential)] public struct SCROLLBARINFO { public uint cbSize; public RECT rect; public int arrow,thumbTop,thumbBottom,reserved; [MarshalAs(UnmanagedType.ByValArray,SizeConst=6)] public uint[] state; }
 [DllImport("user32.dll")] public static extern bool GetScrollBarInfo(IntPtr h,int obj,ref SCROLLBARINFO info);
 [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr h);
 [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr target,int x,int y,int w,int h,IntPtr source,int sx,int sy,uint op);
 [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr h,IntPtr r,IntPtr region,uint flags);
 public static SCROLLBARINFO Bar(IntPtr h,int bar) {var info=new SCROLLBARINFO{cbSize=(uint)Marshal.SizeOf(typeof(SCROLLBARINFO)),state=new uint[6]};if(!GetScrollBarInfo(h,bar,ref info))info.state[0]=0x8000;return info;}
 public static List<IntPtr> Controls(IntPtr h) {var rows=new List<IntPtr>();EnumChildWindows(h,(c,p)=>{if(IsWindowVisible(c))rows.Add(c);return true;},IntPtr.Zero);return rows;}
 [StructLayout(LayoutKind.Sequential)] public struct POINT {public int X,Y;}
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref POINT point);
 public static POINT ClientOrigin(IntPtr h) {var p=new POINT();ClientToScreen(h,ref p);return p;}
 public delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint f);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr h,int i);
 [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr h,uint i,StringBuilder s,int n,uint f);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string s,string k,string v,string f);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string s,string k,string d,StringBuilder b,uint n,string f);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
 [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr addr,UIntPtr size,uint type,uint protect);
 [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr p,IntPtr addr,UIntPtr size,uint type);
 [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr p,IntPtr addr,byte[] b,UIntPtr n,out UIntPtr count);
 [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p,IntPtr addr,byte[] b,UIntPtr n,out UIntPtr count);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] public struct TVITEM { public uint mask;public IntPtr item;public uint state,stateMask;public IntPtr text;public int textMax,image,selectedImage,children;public IntPtr param; }
 public static List<string> Tree(uint pid,IntPtr dialog) {
  IntPtr tree=IntPtr.Zero;EnumChildWindows(dialog,(h,p)=>{if(Class(h)=="SysTreeView32")tree=h;return true;},IntPtr.Zero);
  if(tree==IntPtr.Zero)throw new Exception("Tree control missing");
  var proc=OpenProcess(0x38,false,pid);var remote=VirtualAllocEx(proc,IntPtr.Zero,(UIntPtr)4096,0x3000,4);var names=new List<string>();
  try {VisitTree(tree,SendMessage(tree,0x110A,IntPtr.Zero,IntPtr.Zero),proc,remote,names);return names;}
  finally {VirtualFreeEx(proc,remote,UIntPtr.Zero,0x8000);CloseHandle(proc);}
 }
 static void VisitTree(IntPtr tree,IntPtr item,IntPtr proc,IntPtr remote,List<string> names) {
  while(item!=IntPtr.Zero){var t=new TVITEM{mask=1,item=item,text=IntPtr.Add(remote,256),textMax=1024};int size=Marshal.SizeOf(t);var local=Marshal.AllocHGlobal(size);
   try {Marshal.StructureToPtr(t,local,false);var b=new byte[size];Marshal.Copy(local,b,0,size);UIntPtr count;WriteProcessMemory(proc,remote,b,(UIntPtr)size,out count);if(SendMessage(tree,0x113E,IntPtr.Zero,remote)==IntPtr.Zero)throw new Exception("Tree read failed");var text=new byte[2048];ReadProcessMemory(proc,t.text,text,(UIntPtr)text.Length,out count);names.Add(Encoding.Unicode.GetString(text).Split('\0')[0]);}finally{Marshal.FreeHGlobal(local);}
   VisitTree(tree,SendMessage(tree,0x110A,(IntPtr)4,item),proc,remote,names);item=SendMessage(tree,0x110A,(IntPtr)1,item);
  }
 }
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 public static string Read(string f,string s,string k) {var b=new StringBuilder(4096);GetPrivateProfileString(s,k,"MISSING",b,4096,f);return b.ToString();}
 public static string Class(IntPtr h) {var b=new StringBuilder(256);GetClassName(h,b,256);return b.ToString();}
 public static IntPtr Find(uint id,string cls) {IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==id && IsWindowVisible(h) && Class(h)==cls){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static List<string> Children(IntPtr h) {var rows=new List<string>();EnumChildWindows(h,(c,p)=>{var t=new StringBuilder(1024);GetWindowText(c,t,1024);RECT r;GetWindowRect(c,out r);rows.Add(c+"|"+Class(c)+"|"+t+"|"+r.Left+","+r.Top+","+r.Right+","+r.Bottom);return true;},IntPtr.Zero);return rows;}
 public static List<string> Menus(IntPtr wnd,IntPtr menu,string prefix) {var rows=new List<string>();for(uint i=0;i<GetMenuItemCount(menu);i++){var b=new StringBuilder(1024);GetMenuString(menu,i,b,1024,0x400);var sub=GetSubMenu(menu,(int)i);rows.Add(prefix+b);if(sub!=IntPtr.Zero){SendMessage(wnd,0x117,sub,(IntPtr)i);rows.AddRange(Menus(wnd,sub,prefix+"  "));}}return rows;}
}
'@
function Capture($h,$file) {
 [RestoreNative]::RedrawWindow($h,[IntPtr]::Zero,[IntPtr]::Zero,0x585)|Out-Null
 $r=New-Object RestoreNative+RECT; [RestoreNative]::GetWindowRect($h,[ref]$r) | Out-Null
 $b=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top); $g=[Drawing.Graphics]::FromImage($b);$dc=$g.GetHdc()
 try {if(-not [RestoreNative]::PrintWindow($h,$dc,2)){throw 'PrintWindow failed'}} finally {$g.ReleaseHdc($dc)}
 $b.Save($file);$g.Dispose();$b.Dispose()
}

function Capture-Live($h,$file) {
 [RestoreNative]::RedrawWindow($h,[IntPtr]::Zero,[IntPtr]::Zero,0x585)|Out-Null
 $rect=New-Object RestoreNative+RECT;[RestoreNative]::GetWindowRect($h,[ref]$rect)|Out-Null
 $width=$rect.Right-$rect.Left;$height=$rect.Bottom-$rect.Top
 $bitmap=New-Object Drawing.Bitmap $width,$height;$graphics=[Drawing.Graphics]::FromImage($bitmap)
 $target=$graphics.GetHdc();$source=[RestoreNative]::GetWindowDC($h)
 try {[RestoreNative]::BitBlt($target,0,0,$width,$height,$source,0,0,0xCC0020)|Out-Null}
 finally {$graphics.ReleaseHdc($target);[RestoreNative]::ReleaseDC($h,$source)|Out-Null}
 try {$bitmap.Save($file)}finally{$graphics.Dispose();$bitmap.Dispose()}
}

# Sample the actual window DC during capture, rather than relying only on PrintWindow.
function Capture-Bar($h,$info,$file) {
 $outer=New-Object RestoreNative+RECT; [RestoreNative]::GetWindowRect($h,[ref]$outer)|Out-Null
 $width=$info.rect.Right-$info.rect.Left; $height=$info.rect.Bottom-$info.rect.Top
 $bitmap=New-Object Drawing.Bitmap $width,$height; $graphics=[Drawing.Graphics]::FromImage($bitmap)
 $target=$graphics.GetHdc();$source=[RestoreNative]::GetWindowDC($h)
 try {[RestoreNative]::BitBlt($target,0,0,$width,$height,$source,($info.rect.Left-$outer.Left),($info.rect.Top-$outer.Top),0xCC0020)|Out-Null}
 finally {$graphics.ReleaseHdc($target);[RestoreNative]::ReleaseDC($h,$source)|Out-Null}
 $bitmap.Save($file)
 try {
  for($y=2;$y -lt $height-2;$y++){for($x=2;$x -lt $width-2;$x++){
   $color=$bitmap.GetPixel($x,$y)
   if($color.R -gt 200 -and $color.G -gt 200 -and $color.B -gt 200){throw "White scrollbar pixel in $file at $x,$y"}
  }}
 } finally {$graphics.Dispose();$bitmap.Dispose()}
}
function Exercise-Scrollbars($dialog,$index) {
 $count=0
 foreach($control in [RestoreNative]::Controls($dialog)) {
  foreach($vertical in @($true,$false)) {
   $object=if($vertical){-5}else{-6}
   $info=[RestoreNative]::Bar($control,$object)
   if(($info.state[0] -band 0x18001) -ne 0 -or $info.thumbBottom -le $info.thumbTop){continue}
   $screenX=if($vertical){[int](($info.rect.Left+$info.rect.Right)/2)}else{$info.rect.Left+[int](($info.thumbTop+$info.thumbBottom)/2)}
   $screenY=if($vertical){$info.rect.Top+[int](($info.thumbTop+$info.thumbBottom)/2)}else{[int](($info.rect.Top+$info.rect.Bottom)/2)}
   $packed=($screenY -shl 16) -bor ($screenX -band 0xffff)
   $hit=if($vertical){7}else{6}
   [RestoreNative]::PostMessage($control,0xa1,[IntPtr]$hit,[IntPtr]$packed)|Out-Null
   Start-Sleep -Milliseconds 100
   Capture-Bar $control $info ("$base\drag-{0:d2}-{1}-held.png" -f $index,$count)
   $origin=[RestoreNative]::ClientOrigin($control)
   $delta=45;if($info.thumbTop -gt $info.arrow+10){$delta=-45}
   $localX=$screenX-$origin.X; $localY=$screenY-$origin.Y
   if($vertical){$localY+=$delta}else{$localX+=$delta}
   $move=($localY -shl 16) -bor ($localX -band 0xffff)
   [RestoreNative]::PostMessage($control,0x200,[IntPtr]1,[IntPtr]$move)|Out-Null
   Start-Sleep -Milliseconds 100
   $moved=[RestoreNative]::Bar($control,$object)
   ([RestoreNative]::Class($control)+" vertical="+$vertical+" old="+$info.thumbTop+","+$info.thumbBottom+" new="+$moved.thumbTop+","+$moved.thumbBottom) | Add-Content "$base\scroll-debug.txt"
   Capture-Bar $control $moved ("$base\drag-{0:d2}-{1}-moved.png" -f $index,$count)
   [RestoreNative]::PostMessage($control,0x202,[IntPtr]::Zero,[IntPtr]$move)|Out-Null
   Start-Sleep -Milliseconds 100
   if($moved.thumbTop -eq $info.thumbTop -and $moved.thumbBottom -eq $info.thumbBottom){$script:ScrollFailures.Add("page $index / $control / vertical=$vertical")}
   $count++
  }
 }
 return $count
}

$p=$null;$exitCode=1
try {
 $installed=$SourceProfile;$before=(Get-FileHash $installed).Hash
 function Context-Snapshot {
  $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Classes\SystemFileAssociations')
  $rows=@()
  if($key){foreach($name in ($key.GetSubKeyNames() | Sort-Object)) {
   $verb=$key.OpenSubKey($name+'\shell\FoxicMP');if(!$verb){continue}
   $cmd=$verb.OpenSubKey('command')
   $rows+=@{Extension=$name;Command=$(if($cmd){$cmd.GetValue('')}else{''});Icon=$verb.GetValue('Icon')}
   if($cmd){$cmd.Close()};$verb.Close()
  };$key.Close()}
  return ($rows | ConvertTo-Json -Compress)
 }
 $contextBefore=Context-Snapshot
 Copy-Item $Executable "$dir\FoxicMP64.exe" -Force
 Copy-Item $installed "$dir\FoxicMP64.ini" -Force
 Copy-Item $RussianLanguageDll "$dir\Lang\FoxicMPresources.ru.dll" -Force
 $ini="$dir\FoxicMP64.ini"
 foreach($row in @(@('Settings','ContextMenuEnabled','0'),@('Settings','ContextMenuPrompted','1'),@('Audio','Mute','1'),@('Theme','UseDarkTheme','1'),@('Settings','Language','ru'),@('Dialogs\Options','LastUsedPage','0'))){[RestoreNative]::WritePrivateProfileString($row[0],$row[1],$row[2],$ini)|Out-Null}
 $p=Start-Process "$dir\FoxicMP64.exe" -ArgumentList '/new','/nofocus' -WorkingDirectory $dir -PassThru
 $main=[IntPtr]::Zero
 for($i=0;$i -lt 200;$i++){Start-Sleep -Milliseconds 100;$p.Refresh();if($p.HasExited){throw "Player exited before showing its window: $($p.ExitCode)"};$main=[RestoreNative]::Find($p.Id,'FoxicMP');if($main -ne [IntPtr]::Zero){break}}
 if($main -eq [IntPtr]::Zero){throw 'Main window missing'}
 [RestoreNative]::PostMessage($main,0x111,[IntPtr]815,[IntPtr]::Zero)|Out-Null
 $dialog=[IntPtr]::Zero
 for($i=0;$i -lt 100;$i++){Start-Sleep -Milliseconds 100;$dialog=[RestoreNative]::Find($p.Id,'#32770');if($dialog -ne [IntPtr]::Zero){break}}
 if($dialog -eq [IntPtr]::Zero){throw 'Settings missing'}
 [RestoreNative]::SetWindowPos($dialog,[IntPtr]::Zero,40,40,0,0,0x15)|Out-Null
 Start-Sleep -Milliseconds 200
 $names=[RestoreNative]::Tree($p.Id,$dialog)
 if($names.Count -ne 26){throw "Expected 26 pages, got $($names.Count)"}
 $names | Set-Content "$base\pages.txt" -Encoding UTF8
 [RestoreNative]::SendMessage($dialog,0x465,[IntPtr]5,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 200
 $page=[RestoreNative]::SendMessage($dialog,0x476,[IntPtr]::Zero,[IntPtr]::Zero)
 $check=[RestoreNative]::GetDlgItem($page,10223)
 if($check -eq [IntPtr]::Zero){throw 'Theme checkbox missing'}
 if([RestoreNative]::SendMessage($check,0xf0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1){throw 'Dark checkbox unchecked'}
 # At this point only Player/Interface exist; Apply must precede Formats.
 foreach($mode in @('light','dark')){
  [RestoreNative]::PostMessage($check,0xf5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 200
  # Apply through the property-sheet API, including pages that do not mark generic checkboxes modified.
  if([RestoreNative]::SendMessage($dialog,0x46e,[IntPtr]::Zero,[IntPtr]::Zero) -eq [IntPtr]::Zero){throw 'Apply failed'}
  Start-Sleep -Milliseconds 400
  Capture $dialog "$base\live-$mode.png"
  $image=New-Object Drawing.Bitmap "$base\live-$mode.png"
  try {$pixel=$image.GetPixel(8,$image.Height-10);$level=($pixel.R+$pixel.G+$pixel.B)/3
   if(($mode -eq 'dark' -and $level -gt 100) -or ($mode -eq 'light' -and $level -lt 150)){throw "Theme background mismatch: $mode"}
  } finally {$image.Dispose()}
  # These pages do not register shell associations; Formats must not exist yet.
  foreach($index in @(2,16,22)){
   [RestoreNative]::SendMessage($dialog,0x465,[IntPtr]$index,[IntPtr]::Zero)|Out-Null
   Start-Sleep -Milliseconds 100
   Capture $dialog ("$base\switch-$mode-{0:d2}.png" -f $index)
  }
  [RestoreNative]::SendMessage($dialog,0x465,[IntPtr]5,[IntPtr]::Zero)|Out-Null
 }
 $dragCount=0
 $script:ScrollFailures=New-Object 'Collections.Generic.List[string]'
 foreach($index in 0..25){
  if([RestoreNative]::SendMessage($dialog,0x465,[IntPtr]$index,[IntPtr]::Zero) -eq [IntPtr]::Zero){throw "Failed to select page $index"}
  Start-Sleep -Milliseconds 200
  Capture $dialog ("$base\dark-{0:d2}.png" -f $index)
  Capture-Live $dialog ("$base\screen-{0:d2}.png" -f $index)
  [RestoreNative]::Children($dialog) | Set-Content ("$base\controls-{0:d2}.txt" -f $index) -Encoding UTF8
  if($index -eq 16){
   foreach($control in [RestoreNative]::Controls($dialog)){
    if([RestoreNative]::Class($control) -ne 'SysListView32' -or [RestoreNative]::IsWindowEnabled($control)){continue}
    Capture $control "$base\fullscreen-table.png"
    $table=New-Object Drawing.Bitmap "$base\fullscreen-table.png"
    try {
     for($y=30;$y -lt $table.Height-3;$y++){for($x=3;$x -lt $table.Width-3;$x++){
      $color=$table.GetPixel($x,$y)
      if($color.R -gt 210 -and $color.G -gt 210 -and $color.B -gt 210){throw "White pixel in disabled fullscreen table at $x,$y"}
     }}
    } finally {$table.Dispose()}
   }
  }
  if($index -in @(2,16,22)){$dragCount+=Exercise-Scrollbars $dialog $index}
 }
 [RestoreNative]::PostMessage($dialog,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 300
 [RestoreNative]::PostMessage($main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(-not $p.WaitForExit(10000)){throw 'Close timeout'}
 if($p.ExitCode -ne 0){throw "Player exit $($p.ExitCode)"}
 if((Get-FileHash $installed).Hash -ne $before){throw 'Installed INI changed'}
 if((Context-Snapshot) -ne $contextBefore){throw 'Context menu changed'}
 if($script:ScrollFailures.Count){throw ("Scrollbars did not move: "+($script:ScrollFailures -join "; "))}
 if($dragCount -lt 3){throw "Expected at least 3 scrollbar drags, got $dragCount"}
 "PASS: $dragCount scrollbar drags without white pixels; 26 pages retained, 26 visited, dark/light/live switching, process exit 0, installed profile/context menu unchanged" | Set-Content "$base\result.txt" -Encoding UTF8
 $exitCode=0
} catch {$_ | Out-String | Set-Content "$base\exception.txt" -Encoding UTF8}
finally {if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force};[IO.File]::WriteAllText("$base\exit.txt","$exitCode")}
exit $exitCode
