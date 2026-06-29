[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Find-PythonUserTool {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name
    )

    $PythonScriptDirs = python -c "import os, site, sysconfig; paths = [sysconfig.get_path('scripts', scheme='nt_user'), os.path.join(site.USER_BASE, 'Scripts')]; print('\n'.join(dict.fromkeys(path for path in paths if path)))"
    foreach ( $ScriptDir in $PythonScriptDirs ) {
        if ( Test-Path $ScriptDir ) {
            $env:Path = "${ScriptDir};$env:Path"
            foreach ( $Extension in @('.exe', '.cmd', '.bat', '') ) {
                $ToolPath = Join-Path $ScriptDir "${Name}${Extension}"
                if ( Test-Path $ToolPath ) {
                    return (Resolve-Path $ToolPath).Path
                }
            }
        }
    }

    $Command = Get-Command $Name -ErrorAction 'SilentlyContinue'
    if ( $Command -ne $null ) {
        return $Command.Source
    }

    throw "Unable to locate '${Name}' in Python user scripts or PATH. Checked: $($PythonScriptDirs -join ', ')"
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    $MesonPath = Find-PythonUserTool -Name 'meson'
    $NinjaPath = Find-PythonUserTool -Name 'ninja'
    $CmakeArgs += @(
        "-DMESON_EXECUTABLE=${MesonPath}"
        "-DNINJA_EXECUTABLE=${NinjaPath}"
    )

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
