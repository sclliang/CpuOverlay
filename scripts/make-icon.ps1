# Recreate the code-drawn CPU chip icon; no external image assets are needed.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconPath = Join-Path $PSScriptRoot '..\resources\CpuOverlay.ico'
$sizes = @(16, 32, 48, 64, 256)
$images = @()
foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform($size / 64.0, $size / 64.0)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $background = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 17, 28, 43))
    $mint = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 117, 244, 205), 3.2)
    $white = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 240, 248, 255), 2.8)
    $mint.StartCap = $mint.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.FillEllipse($background, 0, 0, 63, 63)
    $graphics.DrawRectangle($mint, 16, 16, 32, 32)
    foreach ($pin in @(23, 32, 41)) {
        $graphics.DrawLine($mint, $pin, 10, $pin, 15)
        $graphics.DrawLine($mint, $pin, 49, $pin, 54)
        $graphics.DrawLine($mint, 10, $pin, 15, $pin)
        $graphics.DrawLine($mint, 49, $pin, 54, $pin)
    }
    $points = [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(22,35), [System.Drawing.PointF]::new(27,35), [System.Drawing.PointF]::new(31,25), [System.Drawing.PointF]::new(36,40), [System.Drawing.PointF]::new(40,31), [System.Drawing.PointF]::new(43,31))
    $graphics.DrawLines($white, $points)
    $memory = [System.IO.MemoryStream]::new()
    $bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,$memory.ToArray()
    $memory.Dispose(); $graphics.Dispose(); $bitmap.Dispose(); $background.Dispose(); $mint.Dispose(); $white.Dispose()
}
$stream = [System.IO.File]::Create([System.IO.Path]::GetFullPath($iconPath))
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$index].Length); $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($imageBytes in $images) { $writer.Write([byte[]]$imageBytes) }
} finally { $writer.Dispose(); $stream.Dispose() }
