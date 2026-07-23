$dir = "C:\Users\tron\Desktop\beni siz delirttiniz"
Set-Location $dir
# Clean first
& cmd.exe /c "`"$dir\build.bat`" clean" 2>&1 | Out-Null
# Build
$output = & cmd.exe /c "`"$dir\build.bat`"" 2>&1
$output | Out-File -FilePath "$dir\build_result.txt" -Encoding UTF8
Write-Output $output
