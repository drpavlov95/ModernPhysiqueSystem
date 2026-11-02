param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [ValidateRange(128, 4096)]
    [int]$ReviewSize = 1024
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("OverlayChannelExporter" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class OverlayChannelExporter
{
    public static void Export(string sourcePath, string alphaPath, string compositePath, int reviewSize)
    {
        using (var original = new Bitmap(sourcePath))
        using (var source = new Bitmap(reviewSize, reviewSize, PixelFormat.Format32bppArgb))
        using (var graphics = Graphics.FromImage(source))
        using (var alpha = new Bitmap(reviewSize, reviewSize, PixelFormat.Format32bppArgb))
        using (var composite = new Bitmap(reviewSize, reviewSize, PixelFormat.Format32bppArgb))
        {
            graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            graphics.DrawImage(original, 0, 0, reviewSize, reviewSize);

            var rectangle = new Rectangle(0, 0, reviewSize, reviewSize);
            var sourceData = source.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var alphaData = alpha.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            var compositeData = composite.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                var byteCount = Math.Abs(sourceData.Stride) * reviewSize;
                var sourceBytes = new byte[byteCount];
                var alphaBytes = new byte[byteCount];
                var compositeBytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);

                for (var y = 0; y < reviewSize; y++)
                {
                    for (var x = 0; x < reviewSize; x++)
                    {
                        var index = y * sourceData.Stride + x * 4;
                        var opacity = sourceBytes[index + 3];
                        alphaBytes[index] = opacity;
                        alphaBytes[index + 1] = opacity;
                        alphaBytes[index + 2] = opacity;
                        alphaBytes[index + 3] = 255;

                        var checker = ((x / 24 + y / 24) & 1) == 0 ? 48 : 80;
                        var weight = opacity / 255.0;
                        compositeBytes[index] = (byte)Math.Round(sourceBytes[index] * weight + checker * (1.0 - weight));
                        compositeBytes[index + 1] = (byte)Math.Round(sourceBytes[index + 1] * weight + checker * (1.0 - weight));
                        compositeBytes[index + 2] = (byte)Math.Round(sourceBytes[index + 2] * weight + checker * (1.0 - weight));
                        compositeBytes[index + 3] = 255;
                    }
                }

                Marshal.Copy(alphaBytes, 0, alphaData.Scan0, byteCount);
                Marshal.Copy(compositeBytes, 0, compositeData.Scan0, byteCount);
            }
            finally
            {
                source.UnlockBits(sourceData);
                alpha.UnlockBits(alphaData);
                composite.UnlockBits(compositeData);
            }

            alpha.Save(alphaPath, ImageFormat.Png);
            composite.Save(compositePath, ImageFormat.Png);
        }
    }
}
"@
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stem = [System.IO.Path]::GetFileNameWithoutExtension($Source)
$alphaPath = Join-Path $OutputDirectory ($stem + "_alpha.png")
$compositePath = Join-Path $OutputDirectory ($stem + "_composite.png")
[OverlayChannelExporter]::Export($Source, $alphaPath, $compositePath, $ReviewSize)
