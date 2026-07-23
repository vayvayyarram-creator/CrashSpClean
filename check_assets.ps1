Write-Host "=== Assets.hpp ==="
Get-Content 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.hpp'

Write-Host ""
Write-Host "=== Assets.cpp ilk 8 satir ==="
$r = [IO.StreamReader]::new('C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.cpp')
for ($i = 0; $i -lt 8; $i++) {
    $l = $r.ReadLine()
    if ($null -eq $l) { break }
    Write-Host $l.Substring(0, [Math]::Min(120, $l.Length))
}
$r.Close()
