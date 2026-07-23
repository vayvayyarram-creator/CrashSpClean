$srcPath    = 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.hpp'
$cppPath    = 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets.cpp'
$newHppPath = 'C:\Users\tron\Desktop\beni siz delirttiniz\Cheat\Assets_new.hpp'

$hppW = [IO.StreamWriter]::new($newHppPath, $false, [Text.Encoding]::UTF8)
$cppW = [IO.StreamWriter]::new($cppPath,    $false, [Text.Encoding]::UTF8)

$hppW.WriteLine('#pragma once')
$hppW.WriteLine('// Asset extern declarations — definitions are in Assets.cpp')
$hppW.WriteLine()

$cppW.WriteLine('// Asset definitions — compiled once here, not in every TU')
$cppW.WriteLine()

$reader = [IO.StreamReader]::new($srcPath)
while (($line = $reader.ReadLine()) -ne $null) {
    # Skip original pragma/comment lines
    if ($line -match '^\s*#pragma once' -or $line -match '^//') { continue }
    if ($line.Trim() -eq '') { continue }

    # static const unsigned char xxx[] = { ... };
    if ($line -match '^static const unsigned char (\w+)\[\]') {
        $name = $Matches[1]
        # .hpp: extern declaration
        $hppW.WriteLine("extern const unsigned char ${name}[];")
        # .cpp: definition without 'static'
        $defLine = $line -replace '^static ', ''
        $cppW.WriteLine($defLine)
        continue
    }

    # static const int xxx_len = N;
    if ($line -match '^static const int (\w+)') {
        $name = $Matches[1]
        $hppW.WriteLine("extern const int ${name};")
        $defLine = $line -replace '^static ', ''
        $cppW.WriteLine($defLine)
        continue
    }

    # anything else goes to .cpp as-is
    $cppW.WriteLine($line)
}
$reader.Close()
$hppW.Close()
$cppW.Close()

Write-Host "Done. Replacing Assets.hpp..."
Copy-Item $newHppPath $srcPath -Force
Remove-Item $newHppPath -Force
Write-Host "Assets.hpp updated, Assets.cpp created."
