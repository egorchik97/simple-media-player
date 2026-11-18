param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PopupSmoke {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 public delegate bool Callback(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static IntPtr Find(int pid) { IntPtr found=IntPtr.Zero; EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p); if(p==pid){found=h;return false;}return true;},IntPtr.Zero);return found; }
}
'@
$process=Start-Process -FilePath (Resolve-Path $Executable) -ArgumentList '--no-asr' -PassThru
try {
 Start-Sleep -Seconds 3
 $handle=[PopupSmoke]::Find($process.Id)
 if($handle -eq [IntPtr]::Zero){throw 'No window belonging to launched process'}
 $r=New-Object PopupSmoke+Rect
 [void][PopupSmoke]::GetWindowRect($handle,[ref]$r)
 $result=[IntPtr]::Zero
 if([PopupSmoke]::SendMessageTimeout($handle,0,[IntPtr]::Zero,[IntPtr]::Zero,2,3000,[ref]$result) -eq [IntPtr]::Zero){throw 'Window not responding'}
 Write-Output "Responsive popup pid=$($process.Id) dimensions=$($r.Right-$r.Left)x$($r.Bottom-$r.Top)"
 # Settings button: same geometry for compact and expanded. This does not change configuration.
 $x=$r.Right-$r.Left-43; $point=($x -band 65535) -bor (16 -shl 16)
 if([PopupSmoke]::SendMessageTimeout($handle,0x201,[IntPtr]1,[IntPtr]$point,2,5000,[ref]$result) -eq [IntPtr]::Zero){throw 'Click timed out'}
 [void][PopupSmoke]::SendMessageTimeout($handle,0x202,[IntPtr]0,[IntPtr]$point,2,5000,[ref]$result)
 Start-Sleep -Seconds 2
 [void][PopupSmoke]::GetWindowRect($handle,[ref]$r)
 if(($r.Right-$r.Left)-ne 340 -or ($r.Bottom-$r.Top)-ne 392){throw "Settings layout not applied: $($r.Right-$r.Left)x$($r.Bottom-$r.Top)"}
 Write-Output 'Settings navigation resized window to 340x392'
 [void][PopupSmoke]::PostMessage($handle,0x100,[IntPtr]27,[IntPtr]0)
 Start-Sleep -Milliseconds 300
 [void][PopupSmoke]::PostMessage($handle,0x10,[IntPtr]0,[IntPtr]0)
 if(!$process.WaitForExit(15000)){throw 'Shutdown timeout'}
 if($process.ExitCode-ne 0){throw "Exit code $($process.ExitCode)"}
 Write-Output 'Graceful shutdown exit=0'
} finally {
 if(!$process.HasExited){[void][PopupSmoke]::PostMessage([PopupSmoke]::Find($process.Id),0x10,[IntPtr]0,[IntPtr]0)}
}