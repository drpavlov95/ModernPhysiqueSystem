param(
    [Parameter(Mandatory = $true)]
    [string]$Output
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
$outputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$temporaryPng = Join-Path $env:TEMP ("mps-opaque-mask-{0}.png" -f [Guid]::NewGuid())
try {
    $bitmap = New-Object System.Drawing.Bitmap 32, 32, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
        }
        finally {
            $graphics.Dispose()
        }
        $bitmap.Save($temporaryPng, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }

    & texconv -nologo -y -f BC7_UNORM -m 1 -o $outputDirectory $temporaryPng
    if ($LASTEXITCODE -ne 0) {
        throw "texconv failed while creating the opaque Mu hand mask."
    }
    $generatedPath = Join-Path $outputDirectory (
        [System.IO.Path]::GetFileNameWithoutExtension($temporaryPng) + ".dds")
    Move-Item -LiteralPath $generatedPath -Destination $Output -Force
}
finally {
    Remove-Item -LiteralPath $temporaryPng -Force -ErrorAction SilentlyContinue
}
