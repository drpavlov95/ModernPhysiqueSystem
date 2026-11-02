param(
    [Parameter(Mandatory = $true)]
    [string]$SourceOverlay,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$FilePrefix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
if (-not ("OverlayLevelBuilder" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class OverlayLevelBuilder
{
    public static void Create(string sourcePath, string outputDirectory, string filePrefix)
    {
        using (var source = new Bitmap(sourcePath))
        {
            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var sourceData = source.LockBits(rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var byteCount = Math.Abs(sourceData.Stride) * source.Height;
            var sourceBytes = new byte[byteCount];
            try
            {
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);
            }
            finally
            {
                source.UnlockBits(sourceData);
            }

            for (var level = 10; level <= 100; level += 10)
            {
                using (var output = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb))
                {
                    var outputData = output.LockBits(rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                    try
                    {
                        var outputBytes = (byte[])sourceBytes.Clone();
                        for (var index = 3; index < outputBytes.Length; index += 4)
                            outputBytes[index] = (byte)Math.Round(sourceBytes[index] * level / 100.0);
                        Marshal.Copy(outputBytes, 0, outputData.Scan0, byteCount);
                    }
                    finally
                    {
                        output.UnlockBits(outputData);
                    }
                    output.Save(System.IO.Path.Combine(outputDirectory, filePrefix + "_" + level + ".png"), ImageFormat.Png);
                }
            }
        }
    }
}
"@
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
[OverlayLevelBuilder]::Create($SourceOverlay, $OutputDirectory, $FilePrefix)
