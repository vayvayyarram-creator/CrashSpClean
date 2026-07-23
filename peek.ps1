$r = [IO.StreamReader]::new('C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.cpp')
for ($i=0; $i -lt 6; $i++) {
    $l = $r.ReadLine()
    if ($null -eq $l) { break }
    Write-Host $l.Substring(0, [Math]::Min(100, $l.Length))
}
$r.Close()
