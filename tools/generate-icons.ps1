$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$source = [System.Drawing.Image]::FromFile((Join-Path $root 'assets/wiizv-icon.png'))
function IconPng([int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($source, 0, 0, $size, $size)
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    $bytes = $stream.ToArray(); $stream.Dispose()
    return ,$bytes
}
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @($sizes | ForEach-Object { ,(IconPng $_) })
$ico = [System.IO.File]::Create((Join-Path $root 'assets/wiizv.ico'))
$writer = New-Object System.IO.BinaryWriter($ico)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dimension = $sizes[$i] % 256
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
$writer.Dispose()
$densities = @{mdpi=48; hdpi=72; xhdpi=96; xxhdpi=144; xxxhdpi=192}
foreach ($density in $densities.Keys) {
    $folder = Join-Path $root "android/app/src/main/res/mipmap-$density"
    New-Item -ItemType Directory -Force $folder | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $folder 'ic_launcher.png'), (IconPng $densities[$density]))
}
$source.Dispose()
