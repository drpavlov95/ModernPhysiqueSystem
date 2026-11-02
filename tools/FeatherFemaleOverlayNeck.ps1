param(
    [string]$OverlayDirectory,
    [string]$TexconvPath,
    [string]$BackupRoot,

    [ValidateRange(4, 64)]
    [int]$CorePixels = 24,

    [ValidateRange(16, 160)]
    [int]$FeatherPixels = 72
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OverlayDirectory)) {
    $OverlayDirectory = Join-Path $repoRoot "Textures\RleyBodyMorphs\Details\Female"
}
if ([string]::IsNullOrWhiteSpace($TexconvPath)) {
    $TexconvPath = Join-Path $repoRoot "_tmp_tools\directxtex\texconv.exe"
}
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $BackupRoot = Join-Path $repoRoot "backups\DetailsDDS-before-female-neck-alpha-$stamp"
}

$OverlayDirectory = [IO.Path]::GetFullPath($OverlayDirectory)
$TexconvPath = [IO.Path]::GetFullPath($TexconvPath)
$BackupRoot = [IO.Path]::GetFullPath($BackupRoot)
$workRoot = Join-Path $repoRoot "tmp\female-neck-alpha-work"

if (-not (Test-Path -LiteralPath $TexconvPath -PathType Leaf)) {
    throw "texconv.exe not found: $TexconvPath"
}

$expectedNames = @("disabled_overlay.dds")
foreach ($profile in @("heavy", "muscular", "thin")) {
    foreach ($level in @(10, 20, 30, 40, 50, 60, 70, 80, 90, 100)) {
        $expectedNames += "female_$($profile)_$level.dds"
    }
}
$missing = @($expectedNames | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $OverlayDirectory $_) -PathType Leaf)
})
if ($missing.Count -gt 0) {
    throw "Missing female overlays: $($missing -join ', ')"
}

Add-Type -AssemblyName System.Drawing
if (-not ("FemaleNeckOverlayFeather" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public sealed class NeckAlphaStats
{
    public long ChangedPixels;
    public long CanonicalizedTransparentPixels;
    public double RingMeanBefore;
    public int RingMaxBefore;
    public double RingMeanAfter;
    public int RingMaxAfter;
}

public sealed class DdsPatchStats
{
    public long PatchedBlocks;
    public long TotalBlocks;
    public long PatchedBytes;
    public long PreservedBytes;
}

public static class FemaleNeckOverlayFeather
{
    // Exact CBBE neck boundary from femalebody_1.nif (26 vertices). These are
    // NIF texture coordinates, in the same top-left convention used by Mu's
    // Texture2D sampling and DirectXTex's PNG conversion.
    private static readonly double[,] Ring = new double[,]
    {
        { 0.538574, 0.529297 }, { 0.539551, 0.537598 },
        { 0.540527, 0.546875 }, { 0.539062, 0.556152 },
        { 0.536621, 0.566406 }, { 0.531250, 0.575195 },
        { 0.519531, 0.581055 }, { 0.508789, 0.583008 },
        { 0.503906, 0.583496 }, { 0.500000, 0.583496 },
        { 0.496094, 0.583496 }, { 0.490967, 0.583008 },
        { 0.480469, 0.581055 }, { 0.468506, 0.575195 },
        { 0.463135, 0.566406 }, { 0.460938, 0.556152 },
        { 0.459717, 0.546875 }, { 0.460693, 0.537598 },
        { 0.461670, 0.529297 }, { 0.467529, 0.522461 },
        { 0.476562, 0.516602 }, { 0.488281, 0.514648 },
        { 0.500000, 0.514160 }, { 0.511719, 0.514648 },
        { 0.523438, 0.516602 }, { 0.532715, 0.522461 }
    };

    private static double SegmentDistanceSquared(
        double px, double py,
        double ax, double ay,
        double bx, double by)
    {
        double vx = bx - ax;
        double vy = by - ay;
        double lengthSquared = vx * vx + vy * vy;
        double t = lengthSquared > 0.0
            ? ((px - ax) * vx + (py - ay) * vy) / lengthSquared
            : 0.0;
        t = Math.Max(0.0, Math.Min(1.0, t));
        double dx = px - (ax + t * vx);
        double dy = py - (ay + t * vy);
        return dx * dx + dy * dy;
    }

    private static double RingDistancePixels(double x, double y, int width, int height)
    {
        double best = double.MaxValue;
        int count = Ring.GetLength(0);
        for (int i = 0; i < count; ++i)
        {
            int j = (i + 1) % count;
            double distance = SegmentDistanceSquared(
                x, y,
                Ring[i, 0] * (width - 1), Ring[i, 1] * (height - 1),
                Ring[j, 0] * (width - 1), Ring[j, 1] * (height - 1));
            if (distance < best)
                best = distance;
        }
        return Math.Sqrt(best);
    }

    private static double RingDistanceUv(double u, double v)
    {
        double best = double.MaxValue;
        int count = Ring.GetLength(0);
        for (int i = 0; i < count; ++i)
        {
            int j = (i + 1) % count;
            double distance = SegmentDistanceSquared(
                u, v,
                Ring[i, 0], Ring[i, 1],
                Ring[j, 0], Ring[j, 1]);
            if (distance < best)
                best = distance;
        }
        return Math.Sqrt(best);
    }

    private static double SmoothStep(double value)
    {
        value = Math.Max(0.0, Math.Min(1.0, value));
        return value * value * (3.0 - 2.0 * value);
    }

    public static NeckAlphaStats EditTopMip(
        string inputPath,
        string outputPath,
        int corePixels,
        int featherPixels)
    {
        using (var source = new Bitmap(inputPath))
        using (var output = new Bitmap(
            source.Width, source.Height, PixelFormat.Format32bppArgb))
        {
            if (source.PixelFormat != PixelFormat.Format32bppArgb)
                throw new InvalidDataException(
                    "Expected 32-bit ARGB PNG, got " + source.PixelFormat);
            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var sourceData = source.LockBits(
                rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var outputData = output.LockBits(
                rectangle, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = Math.Abs(sourceData.Stride) * source.Height;
                var sourceBytes = new byte[byteCount];
                Marshal.Copy(sourceData.Scan0, sourceBytes, 0, byteCount);
                var outputBytes = (byte[])sourceBytes.Clone();

                double outer = corePixels + featherPixels;
                double minU = 1.0;
                double minV = 1.0;
                double maxU = 0.0;
                double maxV = 0.0;
                for (int i = 0; i < Ring.GetLength(0); ++i)
                {
                    minU = Math.Min(minU, Ring[i, 0]);
                    minV = Math.Min(minV, Ring[i, 1]);
                    maxU = Math.Max(maxU, Ring[i, 0]);
                    maxV = Math.Max(maxV, Ring[i, 1]);
                }
                int minX = Math.Max(0, (int)Math.Floor(minU * (source.Width - 1) - outer - 2));
                int maxX = Math.Min(source.Width - 1, (int)Math.Ceiling(maxU * (source.Width - 1) + outer + 2));
                int minY = Math.Max(0, (int)Math.Floor(minV * (source.Height - 1) - outer - 2));
                int maxY = Math.Min(source.Height - 1, (int)Math.Ceiling(maxV * (source.Height - 1) + outer + 2));

                long changed = 0;
                long canonicalized = 0;
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        double distance = RingDistancePixels(
                            x + 0.5, y + 0.5, source.Width, source.Height);
                        if (distance >= outer)
                            continue;

                        double factor = distance <= corePixels
                            ? 0.0
                            : SmoothStep((distance - corePixels) / featherPixels);
                        int index = y * sourceData.Stride + x * 4 + 3;
                        byte revised = (byte)Math.Round(sourceBytes[index] * factor);
                        if (revised != sourceBytes[index])
                        {
                            outputBytes[index] = revised;
                            ++changed;
                        }
                    }
                }

                var result = new NeckAlphaStats
                {
                    ChangedPixels = changed,
                    CanonicalizedTransparentPixels = canonicalized
                };
                MeasureRingBytes(
                    sourceBytes, sourceData.Stride, source.Width, source.Height,
                    out result.RingMeanBefore, out result.RingMaxBefore);
                MeasureRingBytes(
                    outputBytes, outputData.Stride, output.Width, output.Height,
                    out result.RingMeanAfter, out result.RingMaxAfter);

                Marshal.Copy(outputBytes, 0, outputData.Scan0, byteCount);
                return result;
            }
            finally
            {
                source.UnlockBits(sourceData);
                output.UnlockBits(outputData);
                output.Save(outputPath, ImageFormat.Png);
            }
        }
    }

    private static void MeasureRingBytes(
        byte[] bytes,
        int stride,
        int width,
        int height,
        out double mean,
        out int maximum)
    {
        long total = 0;
        maximum = 0;
        for (int i = 0; i < Ring.GetLength(0); ++i)
        {
            int x = Math.Max(0, Math.Min(width - 1,
                (int)Math.Round(Ring[i, 0] * (width - 1))));
            int y = Math.Max(0, Math.Min(height - 1,
                (int)Math.Round(Ring[i, 1] * (height - 1))));
            int alpha = bytes[y * stride + x * 4 + 3];
            total += alpha;
            maximum = Math.Max(maximum, alpha);
        }
        mean = total / (double)Ring.GetLength(0);
    }

    public static NeckAlphaStats MeasurePng(string path)
    {
        using (var source = new Bitmap(path))
        {
            if (source.PixelFormat != PixelFormat.Format32bppArgb)
                throw new InvalidDataException(
                    "Expected 32-bit ARGB PNG, got " + source.PixelFormat);
            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var data = source.LockBits(
                rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = Math.Abs(data.Stride) * source.Height;
                var bytes = new byte[byteCount];
                Marshal.Copy(data.Scan0, bytes, 0, byteCount);
                var result = new NeckAlphaStats();
                MeasureRingBytes(
                    bytes, data.Stride, source.Width, source.Height,
                    out result.RingMeanAfter, out result.RingMaxAfter);
                return result;
            }
            finally
            {
                source.UnlockBits(data);
            }
        }
    }

    public static int MaximumAlpha(string path)
    {
        using (var source = new Bitmap(path))
        {
            if (source.PixelFormat != PixelFormat.Format32bppArgb)
                throw new InvalidDataException(
                    "Expected 32-bit ARGB PNG, got " + source.PixelFormat);
            var rectangle = new Rectangle(0, 0, source.Width, source.Height);
            var data = source.LockBits(
                rectangle, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int byteCount = Math.Abs(data.Stride) * source.Height;
                var bytes = new byte[byteCount];
                Marshal.Copy(data.Scan0, bytes, 0, byteCount);
                int maximum = 0;
                for (int y = 0; y < source.Height; ++y)
                {
                    for (int x = 0; x < source.Width; ++x)
                        maximum = Math.Max(maximum, bytes[y * data.Stride + x * 4 + 3]);
                }
                return maximum;
            }
            finally
            {
                source.UnlockBits(data);
            }
        }
    }

    private static void ValidateBc7(byte[] bytes, out int width, out int height, out int mipLevels)
    {
        if (bytes.Length < 148 || bytes[0] != (byte)'D' || bytes[1] != (byte)'D' ||
            bytes[2] != (byte)'S' || bytes[3] != (byte)' ')
            throw new InvalidDataException("Not a DDS file");
        height = BitConverter.ToInt32(bytes, 12);
        width = BitConverter.ToInt32(bytes, 16);
        mipLevels = BitConverter.ToInt32(bytes, 28);
        string fourCc = System.Text.Encoding.ASCII.GetString(bytes, 84, 4);
        int dxgiFormat = BitConverter.ToInt32(bytes, 128);
        if (fourCc != "DX10" || dxgiFormat != 98)
            throw new InvalidDataException(
                "Expected DX10 BC7_UNORM (98), got " + fourCc + "/" + dxgiFormat);
    }

    public static DdsPatchStats PatchBc7Collar(
        string originalPath,
        string generatedPath,
        string outputPath,
        int outerPixels)
    {
        byte[] original = File.ReadAllBytes(originalPath);
        byte[] generated = File.ReadAllBytes(generatedPath);
        int width, height, mipLevels;
        int generatedWidth, generatedHeight, generatedMips;
        ValidateBc7(original, out width, out height, out mipLevels);
        ValidateBc7(generated, out generatedWidth, out generatedHeight, out generatedMips);
        if (width != generatedWidth || height != generatedHeight ||
            mipLevels != generatedMips || original.Length != generated.Length)
            throw new InvalidDataException("Generated DDS layout does not match the original");

        byte[] output = (byte[])original.Clone();
        var patchedOffsets = new HashSet<int>();
        int offset = 148;
        long patchedBlocks = 0;
        long totalBlocks = 0;
        double outerUv = outerPixels / (double)Math.Max(width, height);
        for (int mip = 0; mip < mipLevels; ++mip)
        {
            int mipWidth = Math.Max(1, width >> mip);
            int mipHeight = Math.Max(1, height >> mip);
            int blocksWide = Math.Max(1, (mipWidth + 3) / 4);
            int blocksHigh = Math.Max(1, (mipHeight + 3) / 4);
            double halfBlockDiagonal = Math.Sqrt(
                Math.Pow(2.0 / mipWidth, 2.0) +
                Math.Pow(2.0 / mipHeight, 2.0));

            for (int blockY = 0; blockY < blocksHigh; ++blockY)
            {
                for (int blockX = 0; blockX < blocksWide; ++blockX)
                {
                    ++totalBlocks;
                    double u = Math.Min(1.0, (blockX * 4.0 + 2.0) / mipWidth);
                    double v = Math.Min(1.0, (blockY * 4.0 + 2.0) / mipHeight);
                    double distance = RingDistanceUv(u, v);
                    int blockOffset = offset + (blockY * blocksWide + blockX) * 16;
                    if (distance <= outerUv + halfBlockDiagonal)
                    {
                        Buffer.BlockCopy(generated, blockOffset, output, blockOffset, 16);
                        patchedOffsets.Add(blockOffset);
                        ++patchedBlocks;
                    }
                }
            }
            offset += blocksWide * blocksHigh * 16;
        }
        if (offset != original.Length)
            throw new InvalidDataException(
                "DDS mip layout ended at " + offset + " instead of " + original.Length);

        File.WriteAllBytes(outputPath, output);

        // Construction starts from the original byte array. Prove that every
        // byte not replaced by a selected BC7 block stayed exact.
        byte[] written = File.ReadAllBytes(outputPath);
        if (written.Length != original.Length)
            throw new InvalidDataException("Patched DDS length changed");
        for (int i = 0; i < 148; ++i)
        {
            if (written[i] != original[i])
                throw new InvalidDataException("DDS header changed at byte " + i);
        }
        for (int blockOffset = 148; blockOffset < written.Length; blockOffset += 16)
        {
            if (patchedOffsets.Contains(blockOffset))
                continue;
            for (int i = 0; i < 16; ++i)
            {
                if (written[blockOffset + i] != original[blockOffset + i])
                    throw new InvalidDataException(
                        "Compressed data outside collar changed at byte " + (blockOffset + i));
            }
        }
        return new DdsPatchStats
        {
            PatchedBlocks = patchedBlocks,
            TotalBlocks = totalBlocks,
            PatchedBytes = patchedBlocks * 16,
            PreservedBytes = original.Length - patchedBlocks * 16
        };
    }
}
"@
}

function Invoke-Texconv {
    param([string[]]$Arguments)
    & $TexconvPath @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "texconv failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null
$backupFemale = Join-Path $BackupRoot "Female"
New-Item -ItemType Directory -Path $backupFemale -Force | Out-Null

$manifest = @()
foreach ($name in $expectedNames) {
    $source = Join-Path $OverlayDirectory $name
    $backup = Join-Path $backupFemale $name
    if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
        Copy-Item -LiteralPath $source -Destination $backup
    }
    if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
        throw "Backup creation failed: $name"
    }
    $manifest += [ordered]@{
        file = $name
        sha256 = Get-Sha256 $backup
        bytes = (Get-Item -LiteralPath $backup).Length
    }
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content `
    -LiteralPath (Join-Path $BackupRoot "manifest-before.json") -Encoding UTF8

if (Test-Path -LiteralPath $workRoot) {
    $resolvedWork = [IO.Path]::GetFullPath($workRoot)
    $resolvedRepoTemp = [IO.Path]::GetFullPath((Join-Path $repoRoot "tmp"))
    if (-not $resolvedWork.StartsWith($resolvedRepoTemp, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe work directory: $resolvedWork"
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force
}
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null

$report = @()
$completed = $false
try {
    foreach ($name in $expectedNames) {
        $source = Join-Path $OverlayDirectory $name
        $original = Join-Path $backupFemale $name
        $beforeHash = Get-Sha256 $original
        $beforeDirectory = Join-Path $workRoot "before"
        New-Item -ItemType Directory -Path $beforeDirectory -Force | Out-Null
        Invoke-Texconv @("-ft", "png", "-m", "1", "-y", "-o", $beforeDirectory, $original)
        $beforePng = Join-Path $beforeDirectory ([IO.Path]::ChangeExtension($name, ".png"))

        if ($name -eq "disabled_overlay.dds") {
            $maximumAlpha = [FemaleNeckOverlayFeather]::MaximumAlpha($beforePng)
            if ($maximumAlpha -ne 0) {
                throw "disabled_overlay.dds is not fully transparent (max alpha=$maximumAlpha)"
            }
            $report += [ordered]@{
                file = $name
                changed = $false
                reason = "already fully transparent"
                beforeSha256 = $beforeHash
                afterSha256 = $beforeHash
                maximumAlpha = $maximumAlpha
            }
            Remove-Item -LiteralPath $beforeDirectory -Recurse -Force
            continue
        }

        $bytes = [IO.File]::ReadAllBytes($original)
        $width = [BitConverter]::ToInt32($bytes, 16)
        $height = [BitConverter]::ToInt32($bytes, 12)
        $mipLevels = [BitConverter]::ToInt32($bytes, 28)
        $fourCc = [Text.Encoding]::ASCII.GetString($bytes, 84, 4)
        $dxgiFormat = [BitConverter]::ToInt32($bytes, 128)
        if ($width -ne 4096 -or $height -ne 4096 -or $mipLevels -ne 13 -or
            $fourCc -ne "DX10" -or $dxgiFormat -ne 98) {
            throw "Unexpected DDS layout for ${name}: $($width)x$height mips=$mipLevels $fourCc/$dxgiFormat"
        }

        $editedDirectory = Join-Path $workRoot "edited"
        $generatedDirectory = Join-Path $workRoot "generated"
        $validationDirectory = Join-Path $workRoot "validation"
        New-Item -ItemType Directory -Path $editedDirectory -Force | Out-Null
        New-Item -ItemType Directory -Path $generatedDirectory -Force | Out-Null
        New-Item -ItemType Directory -Path $validationDirectory -Force | Out-Null
        $editedPng = Join-Path $editedDirectory ([IO.Path]::ChangeExtension($name, ".png"))
        $editStats = [FemaleNeckOverlayFeather]::EditTopMip(
            $beforePng, $editedPng, $CorePixels, $FeatherPixels)

        Invoke-Texconv @(
            "-f", "BC7_UNORM",
            "-m", "$mipLevels",
            "-sepalpha",
            "-aw", "100.0",
            "-y",
            "-o", $generatedDirectory,
            $editedPng)
        $generatedDds = Join-Path $generatedDirectory $name
        $patchedDds = Join-Path $workRoot $name
        $patchStats = [FemaleNeckOverlayFeather]::PatchBc7Collar(
            $original,
            $generatedDds,
            $patchedDds,
            $CorePixels + $FeatherPixels)

        Invoke-Texconv @("-ft", "png", "-m", "1", "-y", "-o", $validationDirectory, $patchedDds)
        $validationPng = Join-Path $validationDirectory ([IO.Path]::ChangeExtension($name, ".png"))
        $finalStats = [FemaleNeckOverlayFeather]::MeasurePng($validationPng)
        if ($finalStats.RingMaxAfter -gt 2) {
            throw "Neck-ring alpha validation failed for $name (max=$($finalStats.RingMaxAfter))"
        }

        Copy-Item -LiteralPath $patchedDds -Destination $source -Force
        $deployedBytes = [IO.File]::ReadAllBytes($source)
        $deployedWidth = [BitConverter]::ToInt32($deployedBytes, 16)
        $deployedHeight = [BitConverter]::ToInt32($deployedBytes, 12)
        $deployedMips = [BitConverter]::ToInt32($deployedBytes, 28)
        $deployedFourCc = [Text.Encoding]::ASCII.GetString($deployedBytes, 84, 4)
        $deployedDxgi = [BitConverter]::ToInt32($deployedBytes, 128)
        if ($deployedWidth -ne $width -or $deployedHeight -ne $height -or
            $deployedMips -ne $mipLevels -or $deployedFourCc -ne $fourCc -or
            $deployedDxgi -ne $dxgiFormat -or $deployedBytes.Length -ne $bytes.Length) {
            throw "Post-write DDS metadata changed for $name"
        }

        $report += [ordered]@{
            file = $name
            changed = $true
            beforeSha256 = $beforeHash
            afterSha256 = Get-Sha256 $source
            width = $width
            height = $height
            mipLevels = $mipLevels
            format = "BC7_UNORM"
            ringAlphaMeanBefore = [Math]::Round($editStats.RingMeanBefore, 3)
            ringAlphaMaxBefore = $editStats.RingMaxBefore
            ringAlphaMeanAfterEdit = [Math]::Round($editStats.RingMeanAfter, 3)
            ringAlphaMaxAfterEdit = $editStats.RingMaxAfter
            ringAlphaMeanAfterBc7 = [Math]::Round($finalStats.RingMeanAfter, 3)
            ringAlphaMaxAfterBc7 = $finalStats.RingMaxAfter
            changedTopMipPixels = $editStats.ChangedPixels
            canonicalizedTransparentTopMipPixels = $editStats.CanonicalizedTransparentPixels
            patchedBlocksAllMips = $patchStats.PatchedBlocks
            totalBlocksAllMips = $patchStats.TotalBlocks
            patchedBytes = $patchStats.PatchedBytes
            preservedBytes = $patchStats.PreservedBytes
            rgbAndAlphaOutsideSelectedBlocksByteExact = $true
        }

        Remove-Item -LiteralPath $beforeDirectory -Recurse -Force
        Remove-Item -LiteralPath $editedDirectory -Recurse -Force
        Remove-Item -LiteralPath $generatedDirectory -Recurse -Force
        Remove-Item -LiteralPath $validationDirectory -Recurse -Force
        Remove-Item -LiteralPath $patchedDds -Force
        Write-Host "Feathered $name (ring alpha $($editStats.RingMaxBefore) -> $($finalStats.RingMaxAfter))"
    }
    $completed = $true
}
catch {
    Write-Error "Female neck alpha pass failed. Originals are in $BackupRoot. $($_.Exception.Message)"
    throw
}
finally {
    if ($completed -and (Test-Path -LiteralPath $workRoot)) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

$reportPath = Join-Path $BackupRoot "neck-alpha-report.json"
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Backup: $BackupRoot"
Write-Host "Report: $reportPath"
