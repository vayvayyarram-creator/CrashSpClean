$src = 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.cpp'
$tmp = $src + '.tmp'

$r = [IO.StreamReader]::new($src)
$w = [IO.StreamWriter]::new($tmp, $false, [Text.Encoding]::UTF8)

while (($line = $r.ReadLine()) -ne $null) {
    if ($line -match '^const (unsigned char|int) ') {
        $w.WriteLine('extern ' + $line)
    } else {
        $w.WriteLine($line)
    }
}

$r.Close()
$w.Close()

Move-Item $tmp $src -Force
Write-Host "Done."
