param(
    [Parameter(Mandatory = $true)]
    [string]$BaseNormal,

    [Parameter(Mandatory = $true)]
    [string]$DetailMask,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(0, 255)]
    [int]$MaxOpacity = 88,

    [ValidateRange(0, 254)]
    [int]$MaskThreshold = 32
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function New-ArgbBitmap([System.Drawing.Image]$source, [int]$width, [int]$height) {
    $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.DrawImage($source, 0, 0, $width, $height)
    }
    finally {
        $graphics.Dispose()
    }
    return $bitmap
}

$baseSource = [System.Drawing.Image]::FromFile($BaseNormal)
$maskSource = [System.Drawing.Image]::FromFile($DetailMask)

try {
    $baseBitmap = New-ArgbBitmap $baseSource $baseSource.Width $baseSource.Height
    $maskBitmap = New-ArgbBitmap $maskSource $baseSource.Width $baseSource.Height
    $outputBitmap = New-Object System.Drawing.Bitmap($baseSource.Width, $baseSource.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    try {
        $rect = New-Object System.Drawing.Rectangle(0, 0, $baseSource.Width, $baseSource.Height)
        $pixelFormat = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $baseData = $baseBitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $pixelFormat)
        $maskData = $maskBitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $pixelFormat)
        $outputData = $outputBitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $pixelFormat)

        try {
            $byteCount = [Math]::Abs($baseData.Stride) * $baseSource.Height
            $baseBytes = New-Object byte[] $byteCount
            $maskBytes = New-Object byte[] $byteCount
            $outputBytes = New-Object byte[] $byteCount
            [Runtime.InteropServices.Marshal]::Copy($baseData.Scan0, $baseBytes, 0, $byteCount)
            [Runtime.InteropServices.Marshal]::Copy($maskData.Scan0, $maskBytes, 0, $byteCount)

            for ($index = 0; $index -lt $byteCount; $index += 4) {
                $outputBytes[$index] = $baseBytes[$index]
                $outputBytes[$index + 1] = $baseBytes[$index + 1]
                $outputBytes[$index + 2] = $baseBytes[$index + 2]

                # The generated mask is monochrome. Alpha caps its contribution so
                # Mu keeps regenerating the surrounding shape-derived normal map.
                $brightness = [Math]::Max($maskBytes[$index], [Math]::Max($maskBytes[$index + 1], $maskBytes[$index + 2]))
                if ($brightness -le $MaskThreshold) {
                    $outputBytes[$index + 3] = 0
                }
                else {
                    $opacity = [Math]::Round(($brightness - $MaskThreshold) * $MaxOpacity / (255 - $MaskThreshold))
                    $outputBytes[$index + 3] = [byte][Math]::Min($MaxOpacity, $opacity)
                }
            }

            [Runtime.InteropServices.Marshal]::Copy($outputBytes, 0, $outputData.Scan0, $byteCount)
        }
        finally {
            $baseBitmap.UnlockBits($baseData)
            $maskBitmap.UnlockBits($maskData)
            $outputBitmap.UnlockBits($outputData)
        }

        $outputDirectory = Split-Path -Parent $Output
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
        $outputBitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $outputBitmap.Dispose()
        $maskBitmap.Dispose()
        $baseBitmap.Dispose()
    }
}
finally {
    $maskSource.Dispose()
    $baseSource.Dispose()
}
