param(
    [Parameter(Mandatory = $true)]
    [string]$SourceOverlay,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(1, 4)]
    [int]$Radius = 2,

    [ValidateRange(1, 4)]
    [int]$Passes = 2,

    [ValidateRange(0, 32)]
    [int]$PeakTolerance = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("OverlayAlphaSpeckleCleaner" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class OverlayAlphaSpeckleCleaner
{
    public static void Clean(string sourcePath, string outputPath, int radius, int passes, int peakTolerance)
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
                var sourceBytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);
                var outputBytes = (byte[])sourceBytes.Clone();
                var alpha = new byte[source.Width * source.Height];
                for (var y = 0; y < source.Height; y++)
                    for (var x = 0; x < source.Width; x++)
                        alpha[y * source.Width + x] = sourceBytes[y * sourceData.Stride + x * 4 + 3];

                var window = new byte[(radius * 2 + 1) * (radius * 2 + 1) - 1];
                for (var pass = 0; pass < passes; pass++)
                {
                    var filtered = (byte[])alpha.Clone();
                    for (var y = radius; y < source.Height - radius; y++)
                    {
                        for (var x = radius; x < source.Width - radius; x++)
                        {
                            var count = 0;
                            for (var sampleY = y - radius; sampleY <= y + radius; sampleY++)
                            {
                                for (var sampleX = x - radius; sampleX <= x + radius; sampleX++)
                                {
                                    if (sampleX == x && sampleY == y)
                                        continue;
                                    window[count++] = alpha[sampleY * source.Width + sampleX];
                                }
                            }
                            Array.Sort(window, 0, count);
                            var neighborMedian = window[count / 2];
                            var pixel = y * source.Width + x;
                            // Clamp only unsupported bright peaks. Broad ribs
                            // and bony contours have equally strong neighbors
                            // and therefore remain unchanged.
                            if (alpha[pixel] > neighborMedian + peakTolerance)
                                filtered[pixel] = (byte)(neighborMedian + peakTolerance);
                        }
                    }
                    alpha = filtered;
                }

                for (var y = 0; y < source.Height; y++)
                    for (var x = 0; x < source.Width; x++)
                        outputBytes[y * outputData.Stride + x * 4 + 3] = alpha[y * source.Width + x];
                Marshal.Copy(outputBytes, 0, outputData.Scan0, byteCount);
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
[OverlayAlphaSpeckleCleaner]::Clean($SourceOverlay, $Output, $Radius, $Passes, $PeakTolerance)
