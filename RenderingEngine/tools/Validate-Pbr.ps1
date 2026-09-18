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

$captureRoot = Join-Path $repositoryRoot '.artifacts'
$captureRunIdentifier = 'validate-pbr-' + [guid]::NewGuid().ToString('N')
$captureRunDirectory = Join-Path $captureRoot $captureRunIdentifier
$conflictingCaptureRoot = Join-Path $repositoryRoot '.wave1-conflicting-capture-must-not-exist'
if (Test-Path -LiteralPath $captureRunDirectory)
{
    throw ('Wave 1 capture probe run already exists: ' + $captureRunDirectory)
}
if (Test-Path -LiteralPath $conflictingCaptureRoot)
{
    throw ('Wave 1 conflicting-root probe path already exists: ' + $conflictingCaptureRoot)
}

$commandLineCases = @(
    @{ Name = 'help without platform creation'; Arguments = @('--help'); ExitCode = 0; ExpectedText = 'RuntimeConfig v2' },
    @{ Name = 'version without platform creation'; Arguments = @('--version'); ExitCode = 0; ExpectedText = 'runtime-config-v2' },
    @{ Name = 'invalid CLI exit contract'; Arguments = @('--frames', 'not-a-number'); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'invalid target-SPP debug tuple'; Arguments = @('--spp', '2', '--debug-view', 'normal'); ExitCode = 2; ExpectedText = 'Invalid configuration' },
    @{ Name = 'conflicting capture roots'; Arguments = @('--capture', $conflictingCaptureRoot, '--artifact-root', (Join-Path $repositoryRoot '.different-root')); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'unsupported capability exit contract'; Arguments = @('--backend', 'rt-pipeline'); ExitCode = 4; ExpectedText = 'Unsupported configuration' },
    @{ Name = 'unsupported Whitted architecture'; Arguments = @('--transport', 'whitted', '--execution', 'wavefront'); ExitCode = 4; ExpectedText = 'Whitted transport is implemented only' },
    @{ Name = 'Sponza remains asset-gated'; Arguments = @('--scene', 'sponza'); ExitCode = 4; ExpectedText = 'Sponza remains fail-closed' },
    @{ Name = 'removed mixed integrator option'; Arguments = @('--integrator', 'megakernel'); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'removed mixed light proposal option'; Arguments = @('--light-proposal', 'power'); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'removed light sampler alias'; Arguments = @('--light-sampler', 'nee'); ExitCode = 2; ExpectedText = 'Invalid command line' },
    @{ Name = 'unsupported headless execution'; Arguments = @('--headless'); ExitCode = 4; ExpectedText = 'Unsupported configuration' },
    @{ Name = 'Wave 1 live capture'; Arguments = @('--capture', $captureRoot, '--run-id', $captureRunIdentifier, '--frames', '1', '--validation', 'on'); ExitCode = 0; ExpectedText = 'Capture written'; ForbiddenPattern = '\[Vulkan\]' }
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
    if ($commandLineCase.ContainsKey('ForbiddenPattern') -and
        $commandLineText -match $commandLineCase.ForbiddenPattern)
    {
        throw ($commandLineCase.Name + ' emitted a forbidden diagnostic pattern: ' +
            $commandLineCase.ForbiddenPattern)
    }
}

if (Test-Path -LiteralPath $conflictingCaptureRoot)
{
    throw ('Conflicting capture roots created an artifact path: ' + $conflictingCaptureRoot)
}

$captureFiles = @(
    (Join-Path $captureRunDirectory 'captures\image.exr'),
    (Join-Path $captureRunDirectory 'captures\preview.png'),
    (Join-Path $captureRunDirectory 'metadata.json')
)
foreach ($captureFile in $captureFiles)
{
    if (-not (Test-Path -LiteralPath $captureFile -PathType Leaf))
    {
        throw ('Wave 1 capture did not produce the required file: ' + $captureFile)
    }
    if ((Get-Item -LiteralPath $captureFile).Length -le 0)
    {
        throw ('Wave 1 capture produced an empty file: ' + $captureFile)
    }
}

$captureMetadata = Get-Content -Raw -LiteralPath (Join-Path $captureRunDirectory 'metadata.json') |
    ConvertFrom-Json
if ($captureMetadata.evidence_identity.provider_id -ne 'capture:renderer-readback:raw' -or
    $captureMetadata.evidence_identity.origin -ne 'live-runtime' -or
    $captureMetadata.evidence_identity.availability -ne 'fresh' -or
    $captureMetadata.evidence_identity.sample_index -ne 0)
{
    throw 'Wave 1 capture metadata does not identify the fresh first-sample renderer readback.'
}
Write-Host ('Wave 1 capture evidence retained at: ' + $captureRunDirectory)

$runtimeCases = @(
    @{ Name = 'default transport contract (must report PBR)'; Arguments = @('--frames', '2', '--shadow', 'physical'); ExpectedPattern = 'Rendered 2 frames.*2 spp.*PBR path transport' },
    @{ Name = 'PBR physical area light'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', [string]$Frames, '--shadow', 'physical') },
    @{ Name = 'PBR PCF comparison'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '2', '--shadow', 'pcf') },
    @{ Name = 'PBR PCSS comparison'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '2', '--shadow', 'pcss') },
    @{ Name = 'PBR base-color view'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--debug-view', 'base-color') },
    @{ Name = 'PBR normal view'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--debug-view', 'normal') },
    @{ Name = 'PBR roughness view'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--debug-view', 'roughness') },
    @{ Name = 'PBR metallic view'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--debug-view', 'metallic') },
    @{ Name = 'PBR emissive view'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--debug-view', 'emissive') },
    @{ Name = 'PBR fixed seed and target SPP'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--spp', '2', '--seed', '123', '--shadow', 'physical'); ExpectedPattern = 'Rendered 2 frames.*2 spp.*seed 123' },
    @{ Name = 'PBR configured FOV and FIFO VSync'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--fov', '60', '--vsync', 'on') },
    @{ Name = 'PBR configured non-VSync present mode'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--frames', '1', '--vsync', 'off') },
    @{ Name = 'PBR Megakernel with flattened SAH'; Arguments = @('--scene', 'cornell', '--backend', 'gpu-flattened-sah', '--transport', 'pbr', '--execution', 'megakernel', '--direct-lighting', 'mis', '--light-selection', 'power', '--reconstruction', 'progressive-mean', '--frames', '2') },
    @{ Name = 'PBR Wavefront with Ray Query'; Arguments = @('--scene', 'cornell', '--backend', 'ray-query', '--transport', 'pbr', '--execution', 'wavefront', '--direct-lighting', 'mis', '--light-selection', 'power', '--reconstruction', 'progressive-mean', '--frames', '2') },
    @{ Name = 'Environment independent uniform-sphere axis'; Arguments = @('--scene', 'environment-dome', '--backend', 'ray-query', '--execution', 'staged', '--direct-lighting', 'mis', '--light-selection', 'power', '--environment-sampler', 'uniform-sphere', '--frames', '2') },
    @{ Name = 'Environment independent importance-map axis'; Arguments = @('--scene', 'environment-dome', '--backend', 'ray-query', '--execution', 'staged', '--direct-lighting', 'mis', '--light-selection', 'power', '--environment-sampler', 'importance-map', '--frames', '2') },
    @{ Name = 'PBR swapchain resize'; Arguments = @('--transport', 'pbr', '--execution', 'staged', '--resolution', '800x600', '--frames', '90', '--resize-test', '--shadow', 'physical') },
    @{ Name = 'Whitted transport smoke test'; Arguments = @('--transport', 'whitted', '--execution', 'staged', '--frames', '2', '--shadow', 'physical'); ExpectedText = 'Whitted specular transport' }
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
    if ($runtimeText -match '\[Vulkan\]|\[Profiler 拒绝\]|Runtime failure')
    {
        throw ($runtimeCase.Name + ' emitted a Vulkan, profiler, or runtime failure diagnostic.')
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
