$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$saveDir = Join-Path $root "saves\world1"
$exe = Join-Path $root "build\groundwork.exe"

function Snapshot-Saves {
    param([string]$Dir)

    $map = @{}
    if (-not (Test-Path -LiteralPath $Dir)) {
        return $map
    }

    $rootPath = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\', '/')
    Get-ChildItem -LiteralPath $Dir -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($rootPath.Length).TrimStart('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $map[$relative] = [pscustomobject]@{
            Length = $_.Length
            Hash = $hash
            LastWriteUtcTicks = $_.LastWriteTimeUtc.Ticks
        }
    }
    return $map
}

if (-not (Test-Path -LiteralPath $exe)) {
    throw "missing executable: $exe"
}

$before = Snapshot-Saves -Dir $saveDir
& $exe --demo-creature --frames 300
if ($LASTEXITCODE -ne 0) {
    throw "demo creature run failed with exit code $LASTEXITCODE"
}
$after = Snapshot-Saves -Dir $saveDir

$allKeys = @($before.Keys + $after.Keys | Sort-Object -Unique)
$changes = @()
foreach ($key in $allKeys) {
    if (-not $before.ContainsKey($key)) {
        $changes += "created $key"
        continue
    }
    if (-not $after.ContainsKey($key)) {
        $changes += "deleted $key"
        continue
    }
    $a = $before[$key]
    $b = $after[$key]
    if ($a.Length -ne $b.Length -or
        $a.Hash -ne $b.Hash -or
        $a.LastWriteUtcTicks -ne $b.LastWriteUtcTicks) {
        $changes += "modified $key"
    }
}

if ($changes.Count -gt 0) {
    $changes | ForEach-Object { Write-Error $_ }
    throw "demo creature changed save files"
}

Write-Host "PASS: demo creature did not create, delete, or modify saves/world1 files"
