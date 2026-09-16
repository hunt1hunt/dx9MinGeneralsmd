param([int]$hwnd = 1115220, [string]$path = "E:\_auto_step.png", [int]$delayMs = 300)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$ws = New-Object -ComObject WScript.Shell
$ws.AppActivate(9664) | Out-Null
Start-Sleep -Milliseconds $delayMs
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "step shot -> $path"
