param(
    [Parameter(Mandatory = $true)]
    [string]$SourceOverlay,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(1, 16)]
    [int]$BlurRadius = 5,

    [ValidateRange(0.0, 1.0)]
    [double]$StrengthScale = 0.6
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("MorphDetailsAlphaSmoother" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class MorphDetailsAlphaSmoother
{
    public static void Soften(string sourcePath, string outputPath, int radius, double strengthScale)
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
                var outputBytes = (byte[])sourceBytes.Clone();
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);

                var width = source.Width;
                var height = source.Height;
                var integral = new long[(width + 1) * (height + 1)];
                for (var y = 1; y <= height; y++)
                {
                    long rowSum = 0;
                    for (var x = 1; x <= width; x++)
                    {
                        rowSum += sourceBytes[(y - 1) * sourceData.Stride + (x - 1) * 4 + 3];
                        integral[y * (width + 1) + x] = integral[(y - 1) * (width + 1) + x] + rowSum;
                    }
                }

                for (var y = 0; y < height; y++)
                {
                    var top = Math.Max(0, y - radius);
                    var bottom = Math.Min(height - 1, y + radius);
                    for (var x = 0; x < width; x++)
                    {
                        var left = Math.Max(0, x - radius);
                        var right = Math.Min(width - 1, x + radius);
                        var sum = integral[(bottom + 1) * (width + 1) + right + 1]
                            - integral[top * (width + 1) + right + 1]
                            - integral[(bottom + 1) * (width + 1) + left]
                            + integral[top * (width + 1) + left];
                        var average = sum / (double)((right - left + 1) * (bottom - top + 1));
                        outputBytes[y * outputData.Stride + x * 4 + 3] = (byte)Math.Round(
                            Math.Max(0.0, Math.Min(255.0, average * strengthScale)));
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
[MorphDetailsAlphaSmoother]::Soften($SourceOverlay, $Output, $BlurRadius, $StrengthScale)
