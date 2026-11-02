param(
    [Parameter(Mandatory = $true)]
    [string]$NeutralNormal,

    [Parameter(Mandatory = $true)]
    [string[]]$DetailNormals,

    [Parameter(Mandatory = $true)]
    [string]$BaseDetail,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [ValidateRange(1, 96)]
    [int]$MaxOpacity = 34,

    [ValidateSet("muscular", "thin", "heavy")]
    [string]$Profile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("TangentBodyDetailBuilder" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class TangentBodyDetailBuilder
{
    private static Bitmap LoadArgb(string path, int width, int height)
    {
        using (var image = Image.FromFile(path))
        {
            var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
            using (var graphics = Graphics.FromImage(bitmap))
                graphics.DrawImage(image, 0, 0, width, height);
            return bitmap;
        }
    }

    public static void Create(string neutralPath, string[] detailPaths, string baseDetailPath, string outputPath, int maxOpacity, string profile)
    {
        using (var neutral = new Bitmap(neutralPath))
        using (var baseDetail = LoadArgb(baseDetailPath, neutral.Width, neutral.Height))
        using (var output = new Bitmap(neutral.Width, neutral.Height, PixelFormat.Format32bppArgb))
        {
            var details = new Bitmap[detailPaths.Length];
            try
            {
                for (var i = 0; i < detailPaths.Length; i++)
                    details[i] = LoadArgb(detailPaths[i], neutral.Width, neutral.Height);

                var rectangle = new Rectangle(0, 0, neutral.Width, neutral.Height);
                var neutralData = neutral.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                var baseDetailData = baseDetail.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                var outputData = output.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                var detailData = new BitmapData[details.Length];
                try
                {
                    var byteCount = Math.Abs(neutralData.Stride) * neutral.Height;
                    var neutralBytes = new byte[byteCount];
                    var baseDetailBytes = new byte[byteCount];
                    var outputBytes = new byte[byteCount];
                    Marshal.Copy(neutralData.Scan0, neutralBytes, 0, byteCount);
                    Marshal.Copy(baseDetailData.Scan0, baseDetailBytes, 0, byteCount);
                    var detailBytes = new byte[details.Length][];
                    for (var i = 0; i < details.Length; i++)
                    {
                        detailData[i] = details[i].LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                        detailBytes[i] = new byte[byteCount];
                        Marshal.Copy(detailData[i].Scan0, detailBytes[i], 0, byteCount);
                    }

                    var strength = new byte[neutral.Width * neutral.Height];
                    var surface = new bool[neutral.Width * neutral.Height];
                    for (var y = 0; y < neutral.Height; y++)
                    {
                        for (var x = 0; x < neutral.Width; x++)
                        {
                            var index = y * neutralData.Stride + x * 4;
                            var peak = 0;
                            for (var map = 0; map < detailBytes.Length; map++)
                            {
                                peak = Math.Max(peak, Math.Abs(detailBytes[map][index] - neutralBytes[index]));
                                peak = Math.Max(peak, Math.Abs(detailBytes[map][index + 1] - neutralBytes[index + 1]));
                                peak = Math.Max(peak, Math.Abs(detailBytes[map][index + 2] - neutralBytes[index + 2]));
                            }
                            strength[y * neutral.Width + x] = (byte)peak;

                            // Skyrim object-space body maps use a nearly flat
                            // blue/cyan fill outside the UV islands. Reject it
                            // before taking derivatives so island boundaries
                            // cannot become visible seams in the detail map.
                            var nr = neutralBytes[index + 2];
                            var ng = neutralBytes[index + 1];
                            var nb = neutralBytes[index];
                            surface[y * neutral.Width + x] = Math.Abs(nr - 128) + Math.Abs(ng - 128) + Math.Abs(nb - 255) > 24;
                        }
                    }

                    for (var y = 0; y < neutral.Height; y++)
                    {
                        var up = Math.Max(0, y - 1);
                        var down = Math.Min(neutral.Height - 1, y + 1);
                        for (var x = 0; x < neutral.Width; x++)
                        {
                            var left = Math.Max(0, x - 1);
                            var right = Math.Min(neutral.Width - 1, x + 1);
                            var surfaceIndex = y * neutral.Width + x;
                            var interior = surface[surfaceIndex]
                                && surface[y * neutral.Width + left]
                                && surface[y * neutral.Width + right]
                                && surface[up * neutral.Width + x]
                                && surface[down * neutral.Width + x];
                            var dx = interior ? strength[y * neutral.Width + right] - strength[y * neutral.Width + left] : 0;
                            var dy = interior ? strength[down * neutral.Width + x] - strength[up * neutral.Width + x] : 0;
                            if (interior && profile == "heavy" && strength[surfaceIndex] > 6)
                            {
                                var cellX = Math.Cos(x * 0.071 + y * 0.043) + 0.5 * Math.Cos(x * 0.037 - y * 0.089);
                                var cellY = Math.Cos(y * 0.067 - x * 0.029) + 0.4 * Math.Cos(x * 0.021 + y * 0.101);
                                dx += (int)Math.Round(cellX * 3.0);
                                dy += (int)Math.Round(cellY * 3.0);

                                var u = x / (double)(neutral.Width - 1);
                                var v = y / (double)(neutral.Height - 1);
                                var lowerTorso = u > 0.30 && u < 0.70 && v > 0.62 && v < 0.91;
                                var upperThigh = (u > 0.04 && u < 0.30 || u > 0.70 && u < 0.96) && v > 0.67 && v < 0.93;
                                if (lowerTorso || upperThigh)
                                {
                                    // Narrow, gently curved parallel ridges.
                                    // They stay exclusive to Heavy and are
                                    // deliberately weaker than the H100 folds.
                                    var phase = (v * 31.0 + u * 5.0 + Math.Sin(u * 24.0) * 0.22) * Math.PI * 2.0;
                                    var ridgeWindow = Math.Max(0.0, Math.Cos(phase) - 0.55) / 0.45;
                                    var ridgeSlope = Math.Sin(phase) * ridgeWindow * 7.0;
                                    dx += (int)Math.Round(ridgeSlope * 0.35);
                                    dy += (int)Math.Round(ridgeSlope);
                                }
                            }
                            var gradient = Math.Abs(dx) + Math.Abs(dy);
                            var index = y * outputData.Stride + x * 4;
                            var featureOpacity = !interior || gradient <= 4 ? 0 : Math.Min(maxOpacity, (gradient - 4) * 2);

                            var baseOpacity = baseDetailBytes[index + 3] / 255.0;
                            var featureWeight = featureOpacity / 255.0;
                            var outputOpacity = Math.Max(baseOpacity, featureWeight);
                            var divisor = outputOpacity > 0.0 ? outputOpacity : 1.0;
                            var baseX = (baseDetailBytes[index + 2] - 128.0) * baseOpacity;
                            var baseY = (baseDetailBytes[index + 1] - 128.0) * baseOpacity;
                            var featureX = (-dy) * featureWeight;
                            var featureY = dx * featureWeight;

                            outputBytes[index] = 255;
                            outputBytes[index + 1] = (byte)Math.Max(0, Math.Min(255, 128 + (baseY + featureY) / divisor));
                            outputBytes[index + 2] = (byte)Math.Max(0, Math.Min(255, 128 + (baseX + featureX) / divisor));
                            outputBytes[index + 3] = (byte)Math.Round(outputOpacity * 255.0);
                        }
                    }

                    Marshal.Copy(outputBytes, 0, outputData.Scan0, byteCount);
                }
                finally
                {
                    for (var i = 0; i < details.Length; i++)
                        if (detailData[i] != null)
                            details[i].UnlockBits(detailData[i]);
                    neutral.UnlockBits(neutralData);
                    baseDetail.UnlockBits(baseDetailData);
                    output.UnlockBits(outputData);
                }

                output.Save(outputPath, ImageFormat.Png);
            }
            finally
            {
                foreach (var detail in details)
                    if (detail != null)
                        detail.Dispose();
            }
        }
    }
}
"@
}

$outputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[TangentBodyDetailBuilder]::Create($NeutralNormal, $DetailNormals, $BaseDetail, $Output, $MaxOpacity, $Profile)
