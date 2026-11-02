param(
    [Parameter(Mandatory = $true)]
    [string]$SourceOverlay,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(0.0, 1.5)]
    [double]$StrengthScale = 0.5,

    [ValidateRange(2, 96)]
    [int]$EdgeFadePixels = 24
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("MorphDetailsOverlayRefiner" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class MorphDetailsOverlayRefiner
{
    public static void Refine(string sourcePath, string outputPath, double strengthScale, int edgeFadePixels)
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
                var outputBytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);

                var distance = new ushort[source.Width * source.Height];
                var backgroundBlue = sourceBytes[0];
                var backgroundGreen = sourceBytes[1];
                var backgroundRed = sourceBytes[2];
                for (var y = 0; y < source.Height; y++)
                {
                    for (var x = 0; x < source.Width; x++)
                    {
                        var byteIndex = y * sourceData.Stride + x * 4;
                        var backgroundDelta =
                            Math.Abs(sourceBytes[byteIndex] - backgroundBlue) +
                            Math.Abs(sourceBytes[byteIndex + 1] - backgroundGreen) +
                            Math.Abs(sourceBytes[byteIndex + 2] - backgroundRed);
                        distance[y * source.Width + x] =
                            backgroundDelta > 18 ? ushort.MaxValue : (ushort)0;
                    }
                }

                // Inset against the UV background, not against gaps in the
                // authored alpha. This removes the island outline without
                // eroding narrow veins, ribs, cellulite or striae.
                for (var y = 0; y < source.Height; y++)
                {
                    for (var x = 0; x < source.Width; x++)
                    {
                        var pixel = y * source.Width + x;
                        if (distance[pixel] == 0)
                            continue;
                        var nearest = distance[pixel];
                        if (x > 0)
                            nearest = (ushort)Math.Min(nearest, distance[pixel - 1] + 1);
                        if (y > 0)
                            nearest = (ushort)Math.Min(nearest, distance[pixel - source.Width] + 1);
                        distance[pixel] = nearest;
                    }
                }
                for (var y = source.Height - 1; y >= 0; y--)
                {
                    for (var x = source.Width - 1; x >= 0; x--)
                    {
                        var pixel = y * source.Width + x;
                        if (distance[pixel] == 0)
                            continue;
                        var nearest = distance[pixel];
                        if (x + 1 < source.Width)
                            nearest = (ushort)Math.Min(nearest, distance[pixel + 1] + 1);
                        if (y + 1 < source.Height)
                            nearest = (ushort)Math.Min(nearest, distance[pixel + source.Width] + 1);
                        distance[pixel] = nearest;
                    }
                }

                for (var y = 0; y < source.Height; y++)
                {
                    for (var x = 0; x < source.Width; x++)
                    {
                        var byteIndex = y * sourceData.Stride + x * 4;
                        outputBytes[byteIndex] = sourceBytes[byteIndex];
                        outputBytes[byteIndex + 1] = sourceBytes[byteIndex + 1];
                        outputBytes[byteIndex + 2] = sourceBytes[byteIndex + 2];

                        var edgeDistance = Math.Min(
                            distance[y * source.Width + x],
                            Math.Min(
                                Math.Min(x + 1, source.Width - x),
                                Math.Min(y + 1, source.Height - y)));
                        var edgeFade = Math.Max(
                            0.0,
                            Math.Min(1.0, (edgeDistance - 2.0) / edgeFadePixels));
                        edgeFade = edgeFade * edgeFade * (3.0 - 2.0 * edgeFade);
                        outputBytes[byteIndex + 3] = (byte)Math.Round(
                            sourceBytes[byteIndex + 3] * strengthScale * edgeFade);
                    }
                }

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
[MorphDetailsOverlayRefiner]::Refine($SourceOverlay, $Output, $StrengthScale, $EdgeFadePixels)
