param(
    [Parameter(Mandatory = $true)]
    [string] $VulkanSdk,
    [Parameter(Mandatory = $true)]
    [string] $ShaderRoot,
    [Parameter(Mandatory = $true)]
    [string] $OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$dxc = Join-Path $VulkanSdk 'Bin\dxc.exe'
$validator = Join-Path $VulkanSdk 'Bin\spirv-val.exe'
if (-not (Test-Path -LiteralPath $dxc) -or -not (Test-Path -LiteralPath $validator)) {
    throw 'The Vulkan SDK DXC and spirv-val executables are required for L4 shader validation.'
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$specifications = @(
    @{ File = 'software_trace.hlsl'; Entry = 'ResetCountersCS'; Output = 'software_trace_reset.spv' },
    @{ File = 'software_trace.hlsl'; Entry = 'CSMain'; Output = 'software_trace.spv' },
    @{ File = 'software_lbvh_morton.hlsl'; Entry = 'ResetMortonValidationCS'; Output = 'software_lbvh_morton_reset.spv' },
    @{ File = 'software_lbvh_morton.hlsl'; Entry = 'CSMain'; Output = 'software_lbvh_morton.spv' },
    @{ File = 'software_lbvh_radix.hlsl'; Entry = 'HistogramCS'; Output = 'software_lbvh_radix_histogram.spv' },
    @{ File = 'software_lbvh_radix.hlsl'; Entry = 'PrefixCS'; Output = 'software_lbvh_radix_prefix.spv' },
    @{ File = 'software_lbvh_radix.hlsl'; Entry = 'ScatterCS'; Output = 'software_lbvh_radix_scatter.spv' },
    @{ File = 'software_lbvh_radix.hlsl'; Entry = 'ResetStableIdValidationCS'; Output = 'software_lbvh_radix_validate_reset.spv' },
    @{ File = 'software_lbvh_radix.hlsl'; Entry = 'ValidateStableIdsCS'; Output = 'software_lbvh_radix_validate.spv' },
    @{ File = 'software_lbvh_hierarchy.hlsl'; Entry = 'ResetCS'; Output = 'software_lbvh_reset.spv' },
    @{ File = 'software_lbvh_hierarchy.hlsl'; Entry = 'HierarchyCS'; Output = 'software_lbvh_hierarchy.spv' },
    @{ File = 'software_lbvh_bounds.hlsl'; Entry = 'ResetBoundsValidationCS'; Output = 'software_lbvh_bounds_reset.spv' },
    @{ File = 'software_lbvh_bounds.hlsl'; Entry = 'EmitLeavesCS'; Output = 'software_lbvh_emit_leaves.spv' },
    @{ File = 'software_lbvh_bounds.hlsl'; Entry = 'ComputeDepthsCS'; Output = 'software_lbvh_depths.spv' },
    @{ File = 'software_lbvh_bounds.hlsl'; Entry = 'InternalBoundsCS'; Output = 'software_lbvh_internal_bounds.spv' }
)

foreach ($specification in $specifications) {
    $source = Join-Path $ShaderRoot $specification.File
    $output = Join-Path $OutputRoot $specification.Output
    $compilerArguments = @(
        '-T', 'cs_6_6',
        '-E', $specification.Entry,
        '-spirv',
        '-fspv-target-env=vulkan1.3',
        '-fvk-use-dx-layout',
        '-Ges',
        '-WX',
        '-I', $ShaderRoot,
        '-Fo', $output,
        $source
    )
    & $dxc @compilerArguments
    if ($LASTEXITCODE -ne 0) {
        throw "DXC failed for $($specification.File)::$($specification.Entry)."
    }
    & $validator '--target-env' 'vulkan1.3' $output
    if ($LASTEXITCODE -ne 0) {
        throw "spirv-val failed for $($specification.Output)."
    }
}

Write-Output "Validated $($specifications.Count) L4 Compute entrypoints with DXC and spirv-val."
