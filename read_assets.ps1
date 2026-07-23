$p = 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.hpp'
$r = [IO.StreamReader]$p
$i = 0
while (($l = $r.ReadLine()) -ne $null -and $i -lt 12) {
    Write-Host $l.Substring(0, [Math]::Min(160, $l.Length))
    $i++
}
$r.Close()
