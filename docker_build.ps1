$ErrorActionPreference = "Stop"

if (-not $env:DOCKER_BUILDKIT) {
    $env:DOCKER_BUILDKIT = "1"
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker is required"
}

$image = if ($env:PUPHOOK_DOCKER_IMAGE) { $env:PUPHOOK_DOCKER_IMAGE } else { "puphook-builder:ubuntu24.04" }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
$dockerfilePath = Join-Path $repoRoot "docker\\builder.Dockerfile"

function Normalize-PuphookMode {
    param(
        [string] $Value,
        [bool] $AllowBoth
    )

    switch ($Value) {
        { $_ -in @("default", "non-textmode", "non_textmode", "normal", "gui", "0", "1") } { return "default" }
        { $_ -in @("textmode", "text", "2") } { return "textmode" }
        { $_ -in @("both", "all", "3") -and $AllowBoth } { return "both" }
        default { throw "Invalid Puphook mode: $Value" }
    }
}

function Normalize-TextmodeMode {
    param([string] $Value)

    switch ($Value) {
        { $_ -in @("1", "true", "TRUE", "True", "yes", "YES", "Yes", "y", "Y", "on", "ON", "On") } { return "textmode" }
        { $_ -in @("", "0", "false", "FALSE", "False", "no", "NO", "No", "n", "N", "off", "OFF", "Off") } { return "default" }
        default { throw "Invalid textmode value: $Value" }
    }
}

function Get-PuphookModePreferenceFile {
    if ($env:PUPHOOK_MODE_FILE) {
        return $env:PUPHOOK_MODE_FILE
    }

    if ($env:PUPHOOK_CONFIG_DIR) {
        return (Join-Path $env:PUPHOOK_CONFIG_DIR "mode")
    }

    if ($env:APPDATA) {
        return (Join-Path $env:APPDATA "puphook\\mode")
    }

    return (Join-Path $HOME ".config\\puphook\\mode")
}

function Read-PuphookModePreference {
    param([bool] $AllowBoth)

    $modeFile = Get-PuphookModePreferenceFile
    if (-not (Test-Path -LiteralPath $modeFile)) {
        return $null
    }

    $mode = (Get-Content -LiteralPath $modeFile -TotalCount 1)
    return Normalize-PuphookMode $mode $AllowBoth
}

function Write-PuphookModePreference {
    param([string] $Mode)

    $modeFile = Get-PuphookModePreferenceFile
    $modeDir = Split-Path -Parent $modeFile
    New-Item -ItemType Directory -Force -Path $modeDir | Out-Null
    Set-Content -LiteralPath $modeFile -Value $Mode
}

function Select-PuphookMode {
    param([bool] $AllowBoth)

    if ($env:PUPHOOK_MODE) {
        return Normalize-PuphookMode $env:PUPHOOK_MODE $AllowBoth
    }

    if ($env:PUP_BUILD_MODE) {
        return Normalize-PuphookMode $env:PUP_BUILD_MODE $AllowBoth
    }

    if ($null -ne [Environment]::GetEnvironmentVariable("PUPHOOK_TEXTMODE")) {
        return Normalize-TextmodeMode $env:PUPHOOK_TEXTMODE
    }

    if ($null -ne [Environment]::GetEnvironmentVariable("TEXTMODE")) {
        return Normalize-TextmodeMode $env:TEXTMODE
    }

    $savedMode = Read-PuphookModePreference $AllowBoth
    if ($savedMode) {
        return $savedMode
    }

    while ($true) {
        Write-Host ""
        Write-Host "Puphook mode is not set yet."
        Write-Host "1) default  - normal SDL/GUI mode"
        Write-Host "2) textmode - textmode binary"
        if ($AllowBoth) {
            Write-Host "3) both     - build both binaries"
            $answer = Read-Host "Choose mode [1/2/3]"
        } else {
            $answer = Read-Host "Choose mode [1/2]"
        }

        try {
            $mode = Normalize-PuphookMode $answer $AllowBoth
            Write-PuphookModePreference $mode
            Write-Host "Saved Puphook mode '$mode' to $(Get-PuphookModePreferenceFile)."
            return $mode
        } catch {
            Write-Host "Please choose a valid mode."
        }
    }
}

$selectedMode = Select-PuphookMode $true

if (-not $env:PUPHOOK_DOCKER_IMAGE) {
    $needsRebuild = $env:PUPHOOK_DOCKER_REBUILD -eq "1"
    if (-not $needsRebuild) {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & docker image inspect $image 2>$null 1>$null
        $ErrorActionPreference = $previousErrorActionPreference
        $needsRebuild = $LASTEXITCODE -ne 0
    }

    if ($needsRebuild) {
        docker build -t $image -f $dockerfilePath $repoRoot
    }
}

$containerScript = @'
set -euo pipefail
chmod +x build.sh
if [ "${PUPHOOK_DOCKER_INSTALL_PACKAGES:-0}" = "1" ]; then
    chmod +x packages/packages.sh
    ./packages/packages.sh
fi
./build.sh
'@
$containerScript = $containerScript -replace "`r`n", "`n"

docker run --rm `
    -e PUP_BUILD_MODE="${selectedMode}" `
    -e PUPHOOK_TEXTMODE="${env:PUPHOOK_TEXTMODE}" `
    -e PUPHOOK_DOCKER_INSTALL_PACKAGES="${env:PUPHOOK_DOCKER_INSTALL_PACKAGES}" `
    -v "${repoRoot}:/workspace" `
    -w /workspace `
    $image `
    bash -lc $containerScript
