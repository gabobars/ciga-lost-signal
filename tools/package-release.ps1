[CmdletBinding()]
param(
    [switch]$SkipInstallerCompile
)

$ErrorActionPreference = 'Stop'

function Write-Step {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory)][string]$BasePath,
        [Parameter(Mandatory)][string]$FullPath
    )

    $base = [System.IO.Path]::GetFullPath($BasePath).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $full = [System.IO.Path]::GetFullPath($FullPath)
    return $full.Substring($base.Length).TrimStart([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
}

function Test-SourceExcluded {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][System.IO.FileSystemInfo]$Item
    )

    $parts = $RelativePath -split '[\\/]'

    $excludedDirectories = @(
        '.git', '.vs', '.deps', '_deps',
        'build', 'build_x64', 'build_x86',
        'release', 'out', 'CMakeFiles', 'rundir',
        'recovered_christmas_versions'
    )

    $privateDirectoryPattern = '(?i)^(?:backup(?:s)?(?:[-_ ].*)?|recovery(?:[-_ ].*)?|recover(?:[-_ ].*)?|_source-stage|installer-payload|release-assets)$'

    for ($i = 0; $i -lt $parts.Count; $i++) {
        $isDirectorySegment = ($i -lt ($parts.Count - 1)) -or $Item.PSIsContainer

        if (-not $isDirectorySegment) {
            continue
        }

        if ($excludedDirectories -contains $parts[$i]) {
            return $true
        }

        if ($parts[$i] -match $privateDirectoryPattern) {
            return $true
        }
    }

    $name = Split-Path -Leaf $RelativePath
    $extension = [System.IO.Path]::GetExtension($name).ToLowerInvariant()

    if ($name -match '(?i)(\.bak|\.backup)$') { return $true }
    if ($name -match '(?i)\.before-[^\\/]+$') { return $true }
    if ($name -match '(?i)\.current-[^\\/]+$') { return $true }
    if ($name -match '(?i)^(recovery|recover).*\.ps1$') { return $true }
    if ($name -match '(?i)^history_.*\.txt$') { return $true }
    if ($name -match '(?i)^\.CMakeBuildNumber$') { return $true }

    $excludedExtensions = @(
        '.pdb', '.ilk', '.obj', '.o', '.a', '.lib',
        '.exp', '.idb', '.pch', '.tlog', '.lastbuildstate',
        '.log', '.tmp', '.temp', '.zip', '.7z',
        '.exe', '.dll', '.so', '.dylib'
    )

    if ($excludedExtensions -contains $extension) {
        return $true
    }

    return $false
}



function Copy-CleanSourceTree {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][string]$DestinationRoot
    )

    New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
    $items = Get-ChildItem -LiteralPath $SourceRoot -Force -Recurse | Sort-Object {
        $_.FullName.Length
    }

    foreach ($item in $items) {
        $relative = Get-RelativePath -BasePath $SourceRoot -FullPath $item.FullName
        if (Test-SourceExcluded -RelativePath $relative -Item $item) {
            continue
        }

        $destination = Join-Path $DestinationRoot $relative
        if ($item.PSIsContainer) {
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
        } else {
            $destinationDirectory = Split-Path -Parent $destination
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
            Copy-Item -LiteralPath $item.FullName -Destination $destination -Force
        }
    }
}

function Find-InnoCompiler {
    $candidates = @()

    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe')
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe')
    }
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
        $candidates += (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe')
    }

    if ($env:LOCALAPPDATA) {
        $candidates += (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
        $candidates += (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe')
    }
    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($command) {
        $candidates += $command.Source
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }

    return $null
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildSpecPath = Join-Path $ProjectRoot 'buildspec.json'
$ReleaseRoot = Join-Path $ProjectRoot 'release'
$PayloadRoot = Join-Path $ReleaseRoot 'installer-payload'
$InstallerScript = Join-Path $ProjectRoot 'installer\CIGA-Lost-Signal.iss'

if (-not (Test-Path -LiteralPath $BuildSpecPath -PathType Leaf)) {
    throw "Nao encontrei buildspec.json em: $ProjectRoot. Extrai tools e installer para a raiz do projeto CIGA-OBS-Transition."
}
if (-not (Test-Path -LiteralPath $InstallerScript -PathType Leaf)) {
    throw "Nao encontrei o script do Inno Setup: $InstallerScript"
}

$BuildSpec = Get-Content -LiteralPath $BuildSpecPath -Raw | ConvertFrom-Json
$PluginName = [string]$BuildSpec.name
$Version = [string]$BuildSpec.version

if ($PluginName -ne 'ciga-transition') {
    throw "O nome em buildspec.json e '$PluginName'; este instalador espera 'ciga-transition'."
}
if ($Version -notmatch '^\d+\.\d+\.\d+([+-][0-9A-Za-z.-]+)?$') {
    throw "A versao '$Version' em buildspec.json nao parece uma versao valida."
}

$DllCandidates = @(
    (Join-Path $ProjectRoot 'build_x64\rundir\RelWithDebInfo\ciga-transition.dll'),
    (Join-Path $ProjectRoot 'build_x64\RelWithDebInfo\ciga-transition.dll'),
    (Join-Path $ProjectRoot 'build_x64\ciga-transition.dll')
)
$DllPath = $DllCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $DllPath) {
    throw "Nao encontrei ciga-transition.dll. Esperava encontra-la em build_x64\rundir\RelWithDebInfo. Compila o plugin primeiro."
}

$RequiredAssets = @(
    'ciga_transition.effect',
    'signal_lost.png',
    'audio\normal.wav',
    'audio\halloween.wav',
    'audio\november.wav',
    'audio\christmas.wav',
    'locale\en-US.ini'
)
$DataRoot = Join-Path $ProjectRoot 'data'
foreach ($asset in $RequiredAssets) {
    $path = Join-Path $DataRoot $asset
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Falta um recurso necessario para a versao publica: data\$asset"
    }
}

if (-not (Test-Path -LiteralPath $ReleaseRoot)) {
    New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null
}

Write-Step 'Preparar os ficheiros de instalacao'
if (Test-Path -LiteralPath $PayloadRoot) {
    Remove-Item -LiteralPath $PayloadRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $PayloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PayloadRoot 'data\audio') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PayloadRoot 'data\locale') -Force | Out-Null

Copy-Item -LiteralPath $DllPath -Destination (Join-Path $PayloadRoot 'ciga-transition.dll') -Force
foreach ($asset in $RequiredAssets) {
    $source = Join-Path $DataRoot $asset
    $destination = Join-Path (Join-Path $PayloadRoot 'data') $asset
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Write-Host "DLL selecionada: $DllPath"
Write-Host 'Recursos copiados: efeito ativo, imagem, quatro clips de audio e locale.'
Write-Host 'Os ficheiros .bak/.backup e as versoes antigas dos efeitos nao entram no instalador.'

Write-Step 'Criar ZIP do codigo-fonte limpo'
$SourceFolderName = "CIGA-OBS-Transition-$Version-source"
$SourceStageParent = Join-Path $ReleaseRoot '_source-stage'
$SourceStage = Join-Path $SourceStageParent $SourceFolderName
$SourceZip = Join-Path $ReleaseRoot "$SourceFolderName.zip"

if (Test-Path -LiteralPath $SourceStageParent) {
    Remove-Item -LiteralPath $SourceStageParent -Recurse -Force
}
New-Item -ItemType Directory -Path $SourceStageParent -Force | Out-Null
Copy-CleanSourceTree -SourceRoot $ProjectRoot -DestinationRoot $SourceStage

$mustHaveSourceFiles = @('CMakeLists.txt', 'buildspec.json', 'LICENSE', 'README.md', 'src\plugin-main.c', 'src\ciga-audio-source.c', 'data\ciga_transition.effect')
foreach ($relative in $mustHaveSourceFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceStage $relative) -PathType Leaf)) {
        throw "O ZIP de codigo-fonte ficaria incompleto: falta $relative"
    }
}
if (Test-Path -LiteralPath $SourceZip) {
    Remove-Item -LiteralPath $SourceZip -Force
}
Compress-Archive -LiteralPath $SourceStage -DestinationPath $SourceZip -CompressionLevel Optimal
Write-Host "ZIP do codigo-fonte: $SourceZip"

Write-Step 'Validar metadados publicos'
$placeholderWarnings = @()
if ([string]$BuildSpec.author -match '(?i)your name here|example|placeholder') {
    $placeholderWarnings += "buildspec.json ainda tem author='$($BuildSpec.author)'"
}
if ([string]$BuildSpec.website -match '(?i)example\.com|your-|placeholder') {
    $placeholderWarnings += "buildspec.json ainda tem website='$($BuildSpec.website)'"
}
if ([string]$BuildSpec.email -match '(?i)^me@|example\.com|placeholder') {
    $placeholderWarnings += "buildspec.json ainda tem email='$($BuildSpec.email)'"
}
if ($placeholderWarnings.Count -gt 0) {
    Write-Warning 'Antes de tornar o repositorio publico, corrige estes metadados no buildspec.json:'
    foreach ($warning in $placeholderWarnings) { Write-Warning " - $warning" }
}

if (-not $SkipInstallerCompile) {
    Write-Step 'Compilar instalador EXE (Inno Setup)'
    $issText = Get-Content -LiteralPath $InstallerScript -Raw
    $issText = [regex]::Replace($issText, '(?m)^#define MyAppVersion ".*"\s*$', ('#define MyAppVersion "' + $Version + '"'))
    Set-Content -LiteralPath $InstallerScript -Value $issText -Encoding UTF8

    $InnoCompiler = Find-InnoCompiler
    if ($InnoCompiler) {
        & $InnoCompiler $InstallerScript
        if ($LASTEXITCODE -ne 0) {
            throw "O Inno Setup falhou com codigo $LASTEXITCODE."
        }
        $InstallerExe = Join-Path $ReleaseRoot "CIGA-Lost-Signal-Setup-v$Version.exe"
        if (-not (Test-Path -LiteralPath $InstallerExe -PathType Leaf)) {
            throw "O compilador terminou mas nao encontrei o instalador esperado: $InstallerExe"
        }
        Write-Host "Instalador criado: $InstallerExe" -ForegroundColor Green
    } else {
        Write-Warning 'Nao encontrei ISCC.exe. O payload e o ZIP do codigo-fonte foram criados, mas ainda nao foi possivel compilar o EXE.'
        Write-Warning 'Instala o Inno Setup 6 ou 7 e executa novamente este script.'
        Write-Host "Script do instalador: $InstallerScript"
    }
}

Write-Step 'Concluido'
Write-Host "Codigo-fonte: $SourceZip"
Write-Host "Payload do instalador: $PayloadRoot"
Write-Host 'O instalador seleciona o formato de plugins conforme a versao e a instalacao detetada do OBS.'
Write-Host 'Testa o instalador num perfil/PC de teste antes de publicar o Release no GitHub.'



