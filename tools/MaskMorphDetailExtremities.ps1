param(
    [Parameter(Mandatory = $true)]
    [string]$SourceOverlay,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateSet("male", "female")]
    [string]$Sex
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("MorphDetailExtremityMasker" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class MorphDetailExtremityMasker
{
    public static void Apply(string sourcePath, string outputPath, bool female)
    {
        using (var source = new Bitmap(sourcePath))
        using (var output = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb))
        {
            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var sourceData = source.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var outputData = output.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                var byteCount = Math.Abs(sourceData.Stride) * source.Height;
                var bytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, bytes, 0, byteCount);
                for (var y = 0; y < source.Height; y++)
                {
                    var v = (y + 0.5) / source.Height;
                    for (var x = 0; x < source.Width; x++)
                    {
                        var u = (x + 0.5) / source.Width;
                        var outer = u < 0.32 || u > 0.68;
                        var handOrFootIsland = female
                            ? (outer && (v < 0.48 || v > 0.67))
                            : (outer && (v < 0.42 || v > 0.64));
                        if (handOrFootIsland)
                            bytes[y * outputData.Stride + x * 4 + 3] = 0;
                    }
                }
                Marshal.Copy(bytes, 0, outputData.Scan0, byteCount);
            }
            finally
            {
                source.UnlockBits(sourceData);
                output.UnlockBits(outputData);
            }
            output.Save(outputPath, ImageFormat.Png);
        }
    }
}
"@
}

$outputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[MorphDetailExtremityMasker]::Apply($SourceOverlay, $Output, $Sex -eq "female")
