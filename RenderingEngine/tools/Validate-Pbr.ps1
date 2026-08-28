[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [ValidateRange(1, 120)]
    [int] $Frames = 8,

    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$solutionPath = Join-Path $repositoryRoot 'RenderingEngine.sln'
$executablePath = Join-Path $repositoryRoot ('bin\x64\' + $Configuration + '\RenderingEngine.exe')

if (-not $SkipBuild)
{
    $msBuildCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
    )
    $msBuildPath = $msBuildCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1

    if (-not $msBuildPath)
    {
        $msBuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
        if ($msBuildCommand)
        {
            $msBuildPath = $msBuildCommand.Source
        }
    }

    if (-not $msBuildPath)
    {
        throw 'MSBuild.exe was not found. Install Visual Studio C++ tools or add MSBuild to PATH.'
    }

    Write-Host ('Building Vulkan renderer: ' + $Configuration + '|x64')
    $buildArguments = @(
        $solutionPath,
        '/t:Rebuild',
        '/m:1',
        '/p:BuildInParallel=false',
        '/p:UseMultiToolTask=false',
        # Keep CL at one translation unit for deterministic PDB writes. /FS
        # alone is insufficient on some MSVC installations during Rebuild.
        '/p:CL_MPCount=1',
        ('/p:Configuration=' + $Configuration),
        '/p:Platform=x64'
    )
    # Start-Process -Wait follows the whole descendant tree on Windows.
    # MSVC intentionally leaves mspdbsrv/vctip helpers alive, so waiting on the
    # tree can hang after MSBuild has already reported success. Wait only for
    # the actual MSBuild process instead.
    $buildProcess = Start-Process -FilePath $msBuildPath -ArgumentList $buildArguments -UseNewEnvironment -NoNewWindow -PassThru
    $buildProcess.WaitForExit()
    if ($buildProcess.ExitCode -ne 0)
    {
        throw ('MSBuild failed with exit code ' + $buildProcess.ExitCode + '.')
    }
}

if (-not (Test-Path -LiteralPath $executablePath))
{
    throw ('Renderer executable was not found: ' + $executablePath)
}

$unsupportedCaptureRoot = Join-Path $repositoryRoot '.wave0-unsupported-capture-must-not-exist'
if (Test-Path -LiteralPath $unsupportedCaptureRoot)
{
    throw ('Wave 0 no-I/O probe path already exists: ' + $unsupportedCaptureRoot)
}

$commandLineCases = @(
    @{ Name = 'help without platform creation'; Arguments = @('--help'); ExitCode = 0; ExpectedText = 'Wave 0 control contract' },
    @{ Name = 'version without platform creation'; Arguments = @('--version'); ExitCode = 0; ExpectedText = 'runtime-config-v0' },
    @{ Name = 'invalid CLI exit contract'; Arguments = @('--frames', 'not-a-number'); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'invalid target-SPP debug tuple'; Arguments = @('--spp', '2', '--debug-view', 'normal'); ExitCode = 2; ExpectedText = 'Invalid configuration' },
    @{ Name = 'conflicting capture roots'; Arguments = @('--capture', $unsupportedCaptureRoot, '--artifact-root', (Join-Path $repositoryRoot '.different-root')); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'unsupported capability exit contract'; Arguments = @('--backend', 'ray-query'); ExitCode = 4; ExpectedText = 'Unsupported configuration' },
    @{ Name = 'unsupported headless execution'; Arguments = @('--headless'); ExitCode = 4; ExpectedText = 'Unsupported configuration' },
    @{ Name = 'unsupported capture performs no I/O'; Arguments = @('--capture', $unsupportedCaptureRoot); ExitCode = 4; ExpectedText = 'Unsupported configuration' }
)

foreach ($commandLineCase in $commandLineCases)
{
    Write-Host ('Running CLI contract: ' + $commandLineCase.Name)
    $commandLineOutput = & $executablePath @($commandLineCase.Arguments) 2>&1
    $commandLineExitCode = $LASTEXITCODE
    $commandLineOutput | ForEach-Object { Write-Host $_ }
    if ($commandLineExitCode -ne $commandLineCase.ExitCode)
    {
        throw ($commandLineCase.Name + ' returned ' + $commandLineExitCode +
            '; expected ' + $commandLineCase.ExitCode + '.')
    }
    $commandLineText = $commandLineOutput -join [Environment]::NewLine
    if ($commandLineText -notmatch [regex]::Escape($commandLineCase.ExpectedText))
    {
        throw ($commandLineCase.Name + ' did not report expected text: ' +
            $commandLineCase.ExpectedText)
    }
}

if (Test-Path -LiteralPath $unsupportedCaptureRoot)
{
    throw ('Unsupported capture created an artifact path: ' + $unsupportedCaptureRoot)
}

$runtimeCases = @(
    @{ Name = 'default integrator contract (must report PBR)'; Arguments = @('--frames', '2', '--shadow', 'physical'); ExpectedPattern = 'Rendered 2 frames.*2 spp.*PBR path tracer' },
    @{ Name = 'PBR physical area light'; Arguments = @('--integrator', 'pbr', '--frames', [string]$Frames, '--shadow', 'physical') },
    @{ Name = 'PBR PCF comparison'; Arguments = @('--integrator', 'pbr', '--frames', '2', '--shadow', 'pcf') },
    @{ Name = 'PBR PCSS comparison'; Arguments = @('--integrator', 'pbr', '--frames', '2', '--shadow', 'pcss') },
    @{ Name = 'PBR base-color view'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--debug-view', 'base-color') },
    @{ Name = 'PBR normal view'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--debug-view', 'normal') },
    @{ Name = 'PBR roughness view'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--debug-view', 'roughness') },
    @{ Name = 'PBR metallic view'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--debug-view', 'metallic') },
    @{ Name = 'PBR emissive view'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--debug-view', 'emissive') },
    @{ Name = 'PBR fixed seed and target SPP'; Arguments = @('--integrator', 'pbr', '--spp', '2', '--seed', '123', '--shadow', 'physical'); ExpectedPattern = 'Rendered 2 frames.*2 spp.*seed 123' },
    @{ Name = 'PBR configured FOV and FIFO VSync'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--fov', '60', '--vsync', 'on') },
    @{ Name = 'PBR configured non-VSync present mode'; Arguments = @('--integrator', 'pbr', '--frames', '1', '--vsync', 'off') },
    @{ Name = 'PBR swapchain resize'; Arguments = @('--integrator', 'pbr', '--resolution', '800x600', '--frames', '90', '--resize-test', '--shadow', 'physical') },
    @{ Name = 'Whitted compatibility smoke test'; Arguments = @('--integrator', 'whitted', '--frames', '2', '--shadow', 'physical'); ExpectedText = 'Whitted ray tracer' }
)

foreach ($runtimeCase in $runtimeCases)
{
    Write-Host ('Running: ' + $runtimeCase.Name)
    $runtimeArguments = @('--validation', 'on') + @($runtimeCase.Arguments)
    $runtimeOutput = & $executablePath @runtimeArguments 2>&1
    $runtimeExitCode = $LASTEXITCODE
    $runtimeOutput | ForEach-Object { Write-Host $_ }
    if ($runtimeExitCode -ne 0)
    {
        throw ($runtimeCase.Name + ' failed with exit code ' + $runtimeExitCode + '.')
    }
    $runtimeText = $runtimeOutput -join [Environment]::NewLine
    if ($runtimeText -match '\[Vulkan\]')
    {
        throw ($runtimeCase.Name + ' emitted a Vulkan validation/layer diagnostic.')
    }
    if ($runtimeCase.ContainsKey('ExpectedText') -and ($runtimeText -notmatch [regex]::Escape($runtimeCase.ExpectedText)))
    {
        throw ($runtimeCase.Name + ' did not report expected text: ' + $runtimeCase.ExpectedText)
    }
    if ($runtimeCase.ContainsKey('ExpectedPattern') -and ($runtimeText -notmatch $runtimeCase.ExpectedPattern))
    {
        throw ($runtimeCase.Name + ' did not match expected output pattern: ' +
            $runtimeCase.ExpectedPattern)
    }
}

Write-Host 'PBR-default renderer validation suite passed.'
