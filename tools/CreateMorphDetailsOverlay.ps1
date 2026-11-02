param(
    [Parameter(Mandatory = $true)]
    [string]$SourceNormal,

    [Parameter(Mandatory = $true)]
    [string]$NeutralNormal,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(0, 255)]
    [int]$Opacity = 160,

    [ValidateRange(2, 64)]
    [int]$EdgeFadePixels = 16,

    [ValidateSet("muscular", "thin", "heavy")]
    [string]$Profile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("MorphDetailsOverlayBuilder" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class MorphDetailsOverlayBuilder
{
    public static void Create(string sourcePath, string neutralPath, string outputPath, int opacity, int edgeFadePixels, string profile)
    {
        using (var sourceImage = Image.FromFile(sourcePath))
        using (var neutralImage = Image.FromFile(neutralPath))
        using (var source = new Bitmap(sourceImage.Width, sourceImage.Height, PixelFormat.Format32bppArgb))
        using (var neutral = new Bitmap(sourceImage.Width, sourceImage.Height, PixelFormat.Format32bppArgb))
        using (var graphics = Graphics.FromImage(source))
        using (var neutralGraphics = Graphics.FromImage(neutral))
        using (var output = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb))
        {
            graphics.DrawImage(sourceImage, 0, 0, source.Width, source.Height);
            neutralGraphics.DrawImage(neutralImage, 0, 0, neutral.Width, neutral.Height);

            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var sourceData = source.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var neutralData = neutral.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var outputData = output.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                var byteCount = Math.Abs(sourceData.Stride) * source.Height;
                var sourceBytes = new byte[byteCount];
                var neutralBytes = new byte[byteCount];
                var outputBytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);
                Marshal.Copy(neutralData.Scan0, neutralBytes, 0, byteCount);

                var pixelCount = source.Width * source.Height;
                var distance = new ushort[pixelCount];
                var sourceBackground = new int[] { sourceBytes[0], sourceBytes[1], sourceBytes[2] };
                var neutralBackground = new int[] { neutralBytes[0], neutralBytes[1], neutralBytes[2] };

                for (var y = 0; y < source.Height; y++)
                {
                    for (var x = 0; x < source.Width; x++)
                    {
                        var index = y * sourceData.Stride + x * 4;
                        var sourceBackgroundDelta =
                            Math.Abs(sourceBytes[index] - sourceBackground[0]) +
                            Math.Abs(sourceBytes[index + 1] - sourceBackground[1]) +
                            Math.Abs(sourceBytes[index + 2] - sourceBackground[2]);
                        var neutralBackgroundDelta =
                            Math.Abs(neutralBytes[index] - neutralBackground[0]) +
                            Math.Abs(neutralBytes[index + 1] - neutralBackground[1]) +
                            Math.Abs(neutralBytes[index + 2] - neutralBackground[2]);
                        distance[y * source.Width + x] =
                            sourceBackgroundDelta > 18 && neutralBackgroundDelta > 18 ? ushort.MaxValue : (ushort)0;
                    }
                }

                // Manhattan distance to the nearest UV-background pixel. The
                // two passes are enough for a smooth conservative inset and
                // avoid turning an endpoint's shifted island edge into detail.
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
                        var index = y * sourceData.Stride + x * 4;
                        var blue = sourceBytes[index];
                        var green = sourceBytes[index + 1];
                        var red = sourceBytes[index + 2];

                        outputBytes[index] = blue;
                        outputBytes[index + 1] = green;
                        outputBytes[index + 2] = red;

                        var dr = sourceBytes[index + 2] - neutralBytes[index + 2];
                        var dg = sourceBytes[index + 1] - neutralBytes[index + 1];
                        var db = sourceBytes[index] - neutralBytes[index];
                        var textureDelta = Math.Sqrt(dr * dr + dg * dg + db * db);

                        // Preserve the original endpoint normal in RGB. Alpha
                        // only decides where Mu blends those authored details
                        // over its geometry-generated result. A soft nonlinear
                        // ramp retains veins/ribs while rejecting compression
                        // noise and the flat UV background.
                        var normalized = Math.Max(0.0, Math.Min(1.0, (textureDelta - 3.0) / 36.0));
                        var edgeDistance = distance[y * source.Width + x];
                        var edgeFade = Math.Max(0.0, Math.Min(1.0, (edgeDistance - 2.0) / edgeFadePixels));
                        edgeFade = edgeFade * edgeFade * (3.0 - 2.0 * edgeFade);
                        var alpha = Math.Sqrt(normalized) * opacity * edgeFade;
                        outputBytes[index + 3] = (byte)Math.Round(alpha);
                    }
                }

                Marshal.Copy(outputBytes, 0, outputData.Scan0, byteCount);
            }
            finally
            {
                source.UnlockBits(sourceData);
                neutral.UnlockBits(neutralData);
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
[MorphDetailsOverlayBuilder]::Create($SourceNormal, $NeutralNormal, $Output, $Opacity, $EdgeFadePixels, $Profile)
