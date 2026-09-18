param(
    [ValidateRange(2,16)][int]$Frames = 3,
    [ValidateSet('staged','megakernel','wavefront')][string[]]$Execution = @('staged','megakernel','wavefront'),
    [string]$Resolution = '640x360',
    [string]$Scene = 'cornell',
    [ValidateSet('mis','restir-di')][string]$DirectLighting = 'mis',
    [ValidateSet('initial','temporal','spatial','temporal-spatial')][string]$RestirStage = 'initial'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$renderer = Join-Path $repo 'bin/x64/Debug/RenderingEngine.exe'
$oracle = Join-Path $repo 'bin/x64/Debug/RenderingEngine.Showcase.Tests.exe'
$artifactRoot = Join-Path $repo ('.artifacts/p02-film-' + [guid]::NewGuid().ToString('N'))
Write-Output "Frame signal artifact root: $artifactRoot"
Push-Location $repo
try {
    foreach ($architecture in $Execution) {
        $currentImages = @()
        $expectedCamera = $null
        foreach ($index in 1..($Frames + 1)) {
            $isFilm = $index -gt $Frames
            $mode = if ($isFilm) { 'progressive-mean' } else { 'current-frame' }
            $count = if ($isFilm) { $Frames } else { $index }
            $runId = "$architecture-$mode-$count"
            $restirArguments = @()
            if ($DirectLighting -eq 'restir-di') {
                $neighbors = if ($RestirStage -in @('spatial','temporal-spatial')) { 5 } else { 0 }
                $restirArguments = @('--restir-stage', $RestirStage, '--restir-candidates', '1',
                    '--restir-neighbors', "$neighbors", '--restir-bias', 'explicitly-biased',
                    '--animate-many-lights', 'off', '--animate-rigid-occluders', 'off')
            }
            $output = & $renderer --scene $Scene --backend ray-query --transport pbr --execution $architecture --direct-lighting $DirectLighting --light-selection power --environment-sampler uniform-sphere --reconstruction $mode --resolution $Resolution --max-bounce 4 --seed 47 --validation on --frames $count --capture $artifactRoot --artifact-root $artifactRoot --run-id $runId @restirArguments 2>&1
            $runExit = $LASTEXITCODE
            $errors = @($output | Select-String 'VUID-|Validation Error|Runtime failure|Profiler.*拒绝')
            if ($runExit -ne 0 -or $errors.Count -ne 0) {
                $output | Write-Output
                throw "Renderer failed: $runId (exit $runExit)"
            }
            $image = Join-Path $artifactRoot "$runId/captures/image.exr"
            if (!(Test-Path -LiteralPath $image)) { throw "Missing EXR: $image" }
            $metadataPath = Join-Path $artifactRoot "$runId/metadata.json"
            $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
            $camera = $metadata.scene.submitted_camera_position_forward_right_up_fov
            if ($null -eq $camera -or $camera.Count -ne 13) { throw "Missing submitted camera: $runId" }
            $cameraIdentity = $camera | ConvertTo-Json -Compress
            if ($null -eq $expectedCamera) { $expectedCamera = $cameraIdentity }
            elseif ($cameraIdentity -cne $expectedCamera) { throw "Submitted camera differs between captures: $runId" }
            if ($isFilm) { $meanImage = $image } else { $currentImages += $image }
        }
        Write-Output "Architecture: $architecture; scene=$Scene; direct=$DirectLighting; ReSTIR stage=$RestirStage"
        & $oracle --verify-progressive-film $meanImage @currentImages
        if ($LASTEXITCODE -ne 0) { throw "Film arithmetic oracle failed: $architecture" }
    }
    Write-Output "Frame signal evidence: $artifactRoot"
} finally { Pop-Location }
