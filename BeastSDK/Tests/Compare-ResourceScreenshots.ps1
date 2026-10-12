param(
    [string]$First = 'Test/ResourcesDemo/Intermediate/Verification/android-opengl.png',
    [string]$Second = 'Test/ResourcesDemo/Intermediate/Verification/android-vulkan.png'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$firstImage = [Drawing.Bitmap]::new([string](Resolve-Path $First).Path)
$secondImage = [Drawing.Bitmap]::new([string](Resolve-Path $Second).Path)
try {
    $checks = @(@(.05,.05,26,26,26),@(.15,.15,255,0,0),@(.85,.15,0,255,0),@(.15,.85,0,0,255),@(.85,.85,255,255,0),@(.25,.25,255,0,255),@(.35,.46,255,128,128),@(.65,.46,128,255,128),@(.35,.54,128,128,255),@(.35,.68,64,64,255),@(.75,.76,0,255,255))
    foreach ($entry in @(@($First,$firstImage),@($Second,$secondImage))) {
        foreach ($check in $checks) {
            $pixel = $entry[1].GetPixel([int]($check[0]*$entry[1].Width),[int]($check[1]*$entry[1].Height))
            if ([Math]::Abs($pixel.R-$check[2]) -gt 1 -or [Math]::Abs($pixel.G-$check[3]) -gt 1 -or [Math]::Abs($pixel.B-$check[4]) -gt 1) {
                throw "$($entry[0]) pixel $($check[0]),$($check[1]): $pixel"
            }
        }
        "$($entry[0]): 11 expected color checks passed ($($entry[1].Width)x$($entry[1].Height))"
    }
    if ($firstImage.Width -ne $secondImage.Width -or $firstImage.Height -ne $secondImage.Height) { throw 'Different screenshot sizes.' }
    $differences = 0; $samples = 0
    # Exclude the Android status/navigation bars (top/bottom five percent).
    for ($y=[int]($firstImage.Height*.05); $y -lt $firstImage.Height*.95; $y+=11) {
        for ($x=10; $x -lt $firstImage.Width-10; $x+=11) {
            $a=$firstImage.GetPixel($x,$y); $b=$secondImage.GetPixel($x,$y); $samples++
            if ([Math]::Abs($a.R-$b.R) -gt 1 -or [Math]::Abs($a.G-$b.G) -gt 1 -or [Math]::Abs($a.B-$b.B) -gt 1) { $differences++ }
        }
    }
    "Grid comparison: $differences / $samples mismatches (tolerance 1)"
    if ($differences) { throw 'Rendered images differ.' }
} finally { $firstImage.Dispose(); $secondImage.Dispose() }
