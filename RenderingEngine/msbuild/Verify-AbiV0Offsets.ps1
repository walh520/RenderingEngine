[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SpirvPath,

    [Parameter(Mandatory = $true)]
    [string] $GoldenPath,

    [Parameter(Mandatory = $true)]
    [string] $SpirvDisPath,

    [Parameter(Mandatory = $true)]
    [string] $ContractIncludeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

foreach ($requiredPath in @($SpirvPath, $GoldenPath, $SpirvDisPath))
{
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf))
    {
        throw ('ABI v0 verification input was not found: ' + $requiredPath)
    }
}
if (-not (Test-Path -LiteralPath $ContractIncludeRoot -PathType Container))
{
    throw ('ABI v0 contract include root was not found: ' + $ContractIncludeRoot)
}

$golden = Get-Content -Raw -LiteralPath $GoldenPath | ConvertFrom-Json
if ($golden.schema -ne 'vrt.abi-layout/0' -or [int]$golden.abiVersion -ne 1)
{
    throw ('Unsupported ABI layout golden schema or version: ' + [string]$golden.schema)
}
if ([string]$golden.scalarByteOrder -cne 'little-endian')
{
    throw ('ABI v0 scalar byte order must be little-endian, found: ' + [string]$golden.scalarByteOrder)
}
if ([string]$golden.matrixConvention -cne 'four-explicit-rows; column-vector; M-times-v')
{
    throw ('ABI v0 matrix convention is invalid: ' + [string]$golden.matrixConvention)
}

$expectedGoldenRecordNames = @(
    'AbiFloat4',
    'AbiUInt4',
    'AbiMat4Rows',
    'GpuFrameConstantsV0',
    'GpuSceneConstantsV0',
    'GpuVertexV0',
    'GpuGeometryV0',
    'GpuInstanceV0',
    'GpuMaterialV0',
    'GpuLightV0',
    'GpuRayV0',
    'GpuHitV0'
)
$goldenRecordNames = @($golden.records.PSObject.Properties | ForEach-Object { $_.Name })
$missingGoldenRecords = @($expectedGoldenRecordNames | Where-Object { $goldenRecordNames -notcontains $_ })
$unexpectedGoldenRecords = @($goldenRecordNames | Where-Object { $expectedGoldenRecordNames -notcontains $_ })
if ($goldenRecordNames.Count -ne $expectedGoldenRecordNames.Count -or
    $missingGoldenRecords.Count -ne 0 -or $unexpectedGoldenRecords.Count -ne 0)
{
    throw ('ABI v0 golden record set mismatch. Missing=[' + ($missingGoldenRecords -join ', ') +
        '], unexpected=[' + ($unexpectedGoldenRecords -join ', ') + '].')
}

function Read-HlslUintConstants([string] $path)
{
    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        throw ('ABI v0 HLSL contract was not found: ' + $path)
    }
    $constants = @{}
    $source = Get-Content -Raw -LiteralPath $path
    $matches = [regex]::Matches(
        $source,
        '(?m)^\s*static\s+const\s+uint\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)u?\s*;')
    foreach ($match in $matches)
    {
        $name = $match.Groups[1].Value
        if ($constants.ContainsKey($name))
        {
            throw ('Duplicate HLSL uint contract constant: ' + $name)
        }
        $literal = $match.Groups[2].Value
        $constants[$name] = if ($literal.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase))
        {
            [Convert]::ToUInt64($literal.Substring(2), 16)
        }
        else
        {
            [Convert]::ToUInt64($literal, 10)
        }
    }
    return $constants
}

$descriptorRegistryHlsl = Read-HlslUintConstants (Join-Path $ContractIncludeRoot 'DescriptorRegistryV0.hlsli')
$abiVersionHlsl = Read-HlslUintConstants (Join-Path $ContractIncludeRoot 'AbiVersionV0.hlsli')

$disassembly = @(& $SpirvDisPath --no-color $SpirvPath 2>&1)
if ($LASTEXITCODE -ne 0)
{
    throw ('spirv-dis failed with exit code ' + $LASTEXITCODE + ': ' + ($disassembly -join [Environment]::NewLine))
}

$typeNames = @{}
$memberNames = @{}
$memberOffsets = @{}
$scalarSizes = @{}
$vectorTypes = @{}
$structMemberTypes = @{}
$runtimeArrayElementTypes = @{}
$arrayStrides = @{}
$descriptorSets = @{}
$descriptorBindings = @{}

foreach ($lineObject in $disassembly)
{
    $line = [string]$lineObject
    if ($line -match '^\s*OpName\s+(%\S+)\s+"([^"]+)"')
    {
        $typeNames[$Matches[1]] = $Matches[2]
        continue
    }
    if ($line -match '^\s*OpMemberName\s+(%\S+)\s+(\d+)\s+"([^"]+)"')
    {
        $typeId = $Matches[1]
        if (-not $memberNames.ContainsKey($typeId))
        {
            $memberNames[$typeId] = @{}
        }
        $memberNames[$typeId][[int]$Matches[2]] = $Matches[3]
        continue
    }
    if ($line -match '^\s*OpMemberDecorate\s+(%\S+)\s+(\d+)\s+Offset\s+(\d+)')
    {
        $typeId = $Matches[1]
        if (-not $memberOffsets.ContainsKey($typeId))
        {
            $memberOffsets[$typeId] = @{}
        }
        $memberOffsets[$typeId][[int]$Matches[2]] = [int]$Matches[3]
        continue
    }
    if ($line -match '^\s*OpDecorate\s+(%\S+)\s+ArrayStride\s+(\d+)')
    {
        $arrayStrides[$Matches[1]] = [int]$Matches[2]
        continue
    }
    if ($line -match '^\s*OpDecorate\s+(%\S+)\s+DescriptorSet\s+(\d+)')
    {
        $descriptorSets[$Matches[1]] = [int]$Matches[2]
        continue
    }
    if ($line -match '^\s*OpDecorate\s+(%\S+)\s+Binding\s+(\d+)')
    {
        $descriptorBindings[$Matches[1]] = [int]$Matches[2]
        continue
    }
    if ($line -match '^\s*(%\S+)\s*=\s*OpType(?:Float|Int)\s+(\d+)(?:\s+\d+)?\s*$')
    {
        $bitCount = [int]$Matches[2]
        if (($bitCount % 8) -ne 0)
        {
            throw ('SPIR-V scalar width is not byte-addressable: ' + $line)
        }
        $scalarSizes[$Matches[1]] = [int]($bitCount / 8)
        continue
    }
    if ($line -match '^\s*(%\S+)\s*=\s*OpTypeVector\s+(%\S+)\s+(\d+)\s*$')
    {
        $vectorTypes[$Matches[1]] = @{
            ElementType = $Matches[2]
            Count = [int]$Matches[3]
        }
        continue
    }
    if ($line -match '^\s*(%\S+)\s*=\s*OpTypeStruct\s+(.+?)\s*$')
    {
        $structMemberTypes[$Matches[1]] = @($Matches[2].Trim() -split '\s+')
        continue
    }
    if ($line -match '^\s*(%\S+)\s*=\s*OpTypeRuntimeArray\s+(%\S+)\s*$')
    {
        $runtimeArrayElementTypes[$Matches[1]] = $Matches[2]
    }
}

function Get-CanonicalRecordName([string] $spirvTypeName)
{
    if ($spirvTypeName -match '^type\.ConstantBuffer\.(.+)$')
    {
        return $Matches[1]
    }
    return $spirvTypeName
}

$typeSizeCache = @{}
function Get-SpirvTypeSize([string] $typeId)
{
    if ($typeSizeCache.ContainsKey($typeId))
    {
        return [int]$typeSizeCache[$typeId]
    }
    if ($scalarSizes.ContainsKey($typeId))
    {
        $typeSizeCache[$typeId] = [int]$scalarSizes[$typeId]
        return [int]$typeSizeCache[$typeId]
    }
    if ($vectorTypes.ContainsKey($typeId))
    {
        $vector = $vectorTypes[$typeId]
        $typeSizeCache[$typeId] = (Get-SpirvTypeSize ([string]$vector.ElementType)) * [int]$vector.Count
        return [int]$typeSizeCache[$typeId]
    }
    if ($structMemberTypes.ContainsKey($typeId))
    {
        if (-not $memberOffsets.ContainsKey($typeId))
        {
            throw ('SPIR-V structure has no member Offset decorations: ' + $typeId)
        }
        $memberTypes = @($structMemberTypes[$typeId])
        if ($memberOffsets[$typeId].Count -ne $memberTypes.Count)
        {
            throw ('SPIR-V structure has incomplete member Offset decorations: ' + $typeId)
        }
        $span = 0
        for ($memberIndex = 0; $memberIndex -lt $memberTypes.Count; ++$memberIndex)
        {
            if (-not $memberOffsets[$typeId].ContainsKey($memberIndex))
            {
                throw ('SPIR-V structure is missing Offset for member ' + $memberIndex + ': ' + $typeId)
            }
            $memberEnd = [int]$memberOffsets[$typeId][$memberIndex] + (Get-SpirvTypeSize ([string]$memberTypes[$memberIndex]))
            if ($memberEnd -gt $span)
            {
                $span = $memberEnd
            }
        }
        $typeSizeCache[$typeId] = $span
        return $span
    }
    throw ('Cannot determine the SPIR-V byte size of type ' + $typeId + '.')
}

function Compare-RecordLayout(
    [string] $recordName,
    [object] $goldenRecord,
    [object] $actualRecord)
{
    if ([int]$goldenRecord.size -ne [int]$actualRecord.Size)
    {
        throw ('ABI v0 size mismatch for ' + $recordName + ': golden=' + $goldenRecord.size + ', SPIR-V=' + $actualRecord.Size)
    }
    if ([int]$goldenRecord.alignment -ne [int]$actualRecord.Alignment)
    {
        throw ('ABI v0 alignment mismatch for ' + $recordName + ': golden=' + $goldenRecord.alignment + ', ABI rule=' + $actualRecord.Alignment)
    }

    $expectedNames = @($goldenRecord.fields.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actualRecord.Fields.Count -ne $expectedNames.Count)
    {
        throw ('ABI v0 field-count mismatch for ' + $recordName + ': golden=' + $expectedNames.Count + ', SPIR-V=' + $actualRecord.Fields.Count)
    }

    foreach ($expectedProperty in $goldenRecord.fields.PSObject.Properties)
    {
        $fieldName = $expectedProperty.Name
        if (-not $actualRecord.Fields.ContainsKey($fieldName))
        {
            throw ('ABI v0 SPIR-V record ' + $recordName + ' is missing field ' + $fieldName + '.')
        }
        $expectedOffset = [int]$expectedProperty.Value.offset
        $expectedSize = [int]$expectedProperty.Value.size
        $actualField = $actualRecord.Fields[$fieldName]
        if ([int]$actualField.Offset -ne $expectedOffset)
        {
            throw ('ABI v0 offset mismatch for ' + $recordName + '.' + $fieldName + ': golden=' + $expectedOffset + ', SPIR-V=' + $actualField.Offset)
        }
        if ([int]$actualField.Size -ne $expectedSize)
        {
            throw ('ABI v0 field-size mismatch for ' + $recordName + '.' + $fieldName + ': golden=' + $expectedSize + ', SPIR-V=' + $actualField.Size)
        }
    }

    foreach ($actualName in $actualRecord.Fields.Keys)
    {
        if ($expectedNames -notcontains $actualName)
        {
            throw ('ABI v0 SPIR-V record ' + $recordName + ' has unexpected field ' + $actualName + '.')
        }
    }
}

$decoratedRecords = @{}
foreach ($typeId in $memberOffsets.Keys)
{
    if (-not $typeNames.ContainsKey($typeId) -or -not $memberNames.ContainsKey($typeId))
    {
        continue
    }
    $recordName = Get-CanonicalRecordName ([string]$typeNames[$typeId])
    if ($goldenRecordNames -notcontains $recordName)
    {
        continue
    }
    if (-not $structMemberTypes.ContainsKey($typeId))
    {
        throw ('Decorated ABI record is not an OpTypeStruct: ' + $recordName)
    }

    $memberTypes = @($structMemberTypes[$typeId])
    $actualFields = @{}
    foreach ($memberIndex in $memberOffsets[$typeId].Keys)
    {
        if (-not $memberNames[$typeId].ContainsKey($memberIndex))
        {
            throw ('SPIR-V type ' + $typeId + ' has an Offset without an OpMemberName for member ' + $memberIndex + '.')
        }
        if ([int]$memberIndex -ge $memberTypes.Count)
        {
            throw ('SPIR-V type ' + $typeId + ' has an out-of-range decorated member ' + $memberIndex + '.')
        }
        $actualFields[[string]$memberNames[$typeId][$memberIndex]] = [pscustomobject]@{
            Offset = [int]$memberOffsets[$typeId][$memberIndex]
            Size = Get-SpirvTypeSize ([string]$memberTypes[[int]$memberIndex])
        }
    }

    $candidate = [pscustomobject]@{
        Size = Get-SpirvTypeSize $typeId
        Alignment = 16
        Fields = $actualFields
    }
    if (-not $decoratedRecords.ContainsKey($recordName))
    {
        $decoratedRecords[$recordName] = @()
    }
    $decoratedRecords[$recordName] += ,$candidate
}

# HLSL represents these lanes as native vectors, so there is no named SPIR-V
# structure for them. C++ checks alignof/sizeof and this mirror checks every
# golden field against the Vulkan scalar/vector lane rule.
$primitiveLaneFields = @{
    'x' = [pscustomobject]@{ Offset = 0; Size = 4 }
    'y' = [pscustomobject]@{ Offset = 4; Size = 4 }
    'z' = [pscustomobject]@{ Offset = 8; Size = 4 }
    'w' = [pscustomobject]@{ Offset = 12; Size = 4 }
}
$primitiveLaneRecord = [pscustomobject]@{ Size = 16; Alignment = 16; Fields = $primitiveLaneFields }

$validatedRecordCount = 0
foreach ($recordProperty in $golden.records.PSObject.Properties)
{
    $recordName = $recordProperty.Name
    if ($recordName -eq 'AbiFloat4' -or $recordName -eq 'AbiUInt4')
    {
        Compare-RecordLayout $recordName $recordProperty.Value $primitiveLaneRecord
        ++$validatedRecordCount
        continue
    }
    if (-not $decoratedRecords.ContainsKey($recordName) -or $decoratedRecords[$recordName].Count -eq 0)
    {
        throw ('ABI v0 golden record was not found in SPIR-V decorations: ' + $recordName)
    }
    foreach ($candidate in $decoratedRecords[$recordName])
    {
        Compare-RecordLayout $recordName $recordProperty.Value $candidate
    }
    ++$validatedRecordCount
}

# Structured-buffer ArrayStride is a second, independent size decoration.
$stridesByRecord = @{}
foreach ($arrayTypeId in $runtimeArrayElementTypes.Keys)
{
    if (-not $arrayStrides.ContainsKey($arrayTypeId))
    {
        continue
    }
    $elementTypeId = [string]$runtimeArrayElementTypes[$arrayTypeId]
    if (-not $typeNames.ContainsKey($elementTypeId))
    {
        continue
    }
    $recordName = Get-CanonicalRecordName ([string]$typeNames[$elementTypeId])
    if ($goldenRecordNames -contains $recordName)
    {
        $stridesByRecord[$recordName] = [int]$arrayStrides[$arrayTypeId]
    }
}
foreach ($recordName in @('GpuVertexV0', 'GpuGeometryV0', 'GpuInstanceV0', 'GpuMaterialV0', 'GpuLightV0', 'GpuRayV0', 'GpuHitV0'))
{
    if (-not $stridesByRecord.ContainsKey($recordName))
    {
        throw ('ABI v0 structured record has no SPIR-V ArrayStride: ' + $recordName)
    }
    $goldenRecord = $golden.records.PSObject.Properties[$recordName].Value
    if ([int]$stridesByRecord[$recordName] -ne [int]$goldenRecord.size)
    {
        throw ('ABI v0 ArrayStride mismatch for ' + $recordName + ': golden=' + $goldenRecord.size + ', SPIR-V=' + $stridesByRecord[$recordName])
    }
}

# ABI v0 is frozen. These mirrors make edits to the JSON registry fail even for
# reserved entries that the Wave 0 probe cannot materialize as resources.
$frozenSets = [ordered]@{
    frame = 0; scene = 1; traversalBackend = 2; wavefrontReserved = 3
    reconstructionReserved = 4; restirReserved = 5; debugProfiler = 6
}
$frozenBindings = [ordered]@{
    'frame.constants' = 0; 'scene.constants' = 0; 'scene.vertices' = 1
    'scene.indices' = 2; 'scene.geometries' = 3; 'scene.instances' = 4
    'scene.materials' = 5; 'scene.lights' = 6; 'scene.textures' = 16
    'scene.samplers' = 17; 'debugProfiler.constants' = 0
}
$frozenHlslDescriptorConstants = [ordered]@{
    kDescriptorSetFrameV0 = [uint64]$frozenSets.frame
    kDescriptorSetSceneV0 = [uint64]$frozenSets.scene
    kDescriptorSetTraversalBackendV0 = [uint64]$frozenSets.traversalBackend
    kDescriptorSetWavefrontReservedV0 = [uint64]$frozenSets.wavefrontReserved
    kDescriptorSetReconstructionReservedV0 = [uint64]$frozenSets.reconstructionReserved
    kDescriptorSetRestirReservedV0 = [uint64]$frozenSets.restirReserved
    kDescriptorSetDebugProfilerV0 = [uint64]$frozenSets.debugProfiler
    kFrameBindingConstantsV0 = [uint64]$frozenBindings.'frame.constants'
    kSceneBindingConstantsV0 = [uint64]$frozenBindings.'scene.constants'
    kSceneBindingVerticesV0 = [uint64]$frozenBindings.'scene.vertices'
    kSceneBindingIndicesV0 = [uint64]$frozenBindings.'scene.indices'
    kSceneBindingGeometriesV0 = [uint64]$frozenBindings.'scene.geometries'
    kSceneBindingInstancesV0 = [uint64]$frozenBindings.'scene.instances'
    kSceneBindingMaterialsV0 = [uint64]$frozenBindings.'scene.materials'
    kSceneBindingLightsV0 = [uint64]$frozenBindings.'scene.lights'
    kSceneBindingTexturesV0 = [uint64]$frozenBindings.'scene.textures'
    kSceneBindingSamplersV0 = [uint64]$frozenBindings.'scene.samplers'
    kDebugProfilerBindingConstantsV0 = [uint64]$frozenBindings.'debugProfiler.constants'
}
foreach ($entry in $frozenSets.GetEnumerator())
{
    $property = $golden.descriptorRegistry.sets.PSObject.Properties[$entry.Key]
    if ($null -eq $property -or [int]$property.Value -ne [int]$entry.Value)
    {
        throw ('ABI v0 descriptor-set registry mismatch for ' + $entry.Key + '.')
    }
}
foreach ($entry in $frozenBindings.GetEnumerator())
{
    $property = $golden.descriptorRegistry.bindings.PSObject.Properties[$entry.Key]
    if ($null -eq $property -or [int]$property.Value -ne [int]$entry.Value)
    {
        throw ('ABI v0 descriptor-binding registry mismatch for ' + $entry.Key + '.')
    }
}
if (@($golden.descriptorRegistry.sets.PSObject.Properties).Count -ne $frozenSets.Count -or
    @($golden.descriptorRegistry.bindings.PSObject.Properties).Count -ne $frozenBindings.Count)
{
    throw 'ABI v0 descriptor registry has missing or unexpected entries.'
}
if ($descriptorRegistryHlsl.Count -ne $frozenHlslDescriptorConstants.Count)
{
    throw 'ABI v0 HLSL descriptor registry has missing or unexpected uint constants.'
}
foreach ($entry in $frozenHlslDescriptorConstants.GetEnumerator())
{
    if (-not $descriptorRegistryHlsl.ContainsKey($entry.Key) -or
        [uint64]$descriptorRegistryHlsl[$entry.Key] -ne [uint64]$entry.Value)
    {
        throw ('ABI v0 HLSL descriptor constant mismatch: ' + $entry.Key)
    }
}
$frozenHlslVersionConstants = [ordered]@{
    kAbiVersionV0 = [uint64]$golden.abiVersion
    kInvalidIdV0 = [uint64]4294967295
}
if ($abiVersionHlsl.Count -ne $frozenHlslVersionConstants.Count)
{
    throw 'ABI v0 HLSL version contract has missing or unexpected uint constants.'
}
foreach ($entry in $frozenHlslVersionConstants.GetEnumerator())
{
    if (-not $abiVersionHlsl.ContainsKey($entry.Key) -or
        [uint64]$abiVersionHlsl[$entry.Key] -ne [uint64]$entry.Value)
    {
        throw ('ABI v0 HLSL version constant mismatch: ' + $entry.Key)
    }
}

$expectedResources = @(
    [pscustomobject]@{ Name = 'gFrame'; Set = [int]$golden.descriptorRegistry.sets.frame; Binding = [int]$golden.descriptorRegistry.bindings.'frame.constants' }
    [pscustomobject]@{ Name = 'gScene'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.constants' }
    [pscustomobject]@{ Name = 'gVertices'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.vertices' }
    [pscustomobject]@{ Name = 'gIndices'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.indices' }
    [pscustomobject]@{ Name = 'gGeometries'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.geometries' }
    [pscustomobject]@{ Name = 'gInstances'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.instances' }
    [pscustomobject]@{ Name = 'gMaterials'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.materials' }
    [pscustomobject]@{ Name = 'gLights'; Set = [int]$golden.descriptorRegistry.sets.scene; Binding = [int]$golden.descriptorRegistry.bindings.'scene.lights' }
    [pscustomobject]@{ Name = 'gRays'; Set = [int]$golden.layoutProbeBindings.rayInput.set; Binding = [int]$golden.layoutProbeBindings.rayInput.binding }
    [pscustomobject]@{ Name = 'gHits'; Set = [int]$golden.layoutProbeBindings.hitInput.set; Binding = [int]$golden.layoutProbeBindings.hitInput.binding }
    [pscustomobject]@{ Name = 'gProbeOutput'; Set = [int]$golden.layoutProbeBindings.rawOutput.set; Binding = [int]$golden.layoutProbeBindings.rawOutput.binding }
)
$expectedLayoutProbeNames = @('testOnly', 'rayInput', 'hitInput', 'rawOutput')
$actualLayoutProbeNames = @($golden.layoutProbeBindings.PSObject.Properties | ForEach-Object { $_.Name })
if ($actualLayoutProbeNames.Count -ne $expectedLayoutProbeNames.Count -or
    @($expectedLayoutProbeNames | Where-Object { $actualLayoutProbeNames -notcontains $_ }).Count -ne 0 -or
    @($actualLayoutProbeNames | Where-Object { $expectedLayoutProbeNames -notcontains $_ }).Count -ne 0)
{
    throw 'ABI v0 layout-probe registry has missing or unexpected entries.'
}
foreach ($probeBindingName in @('rayInput', 'hitInput', 'rawOutput'))
{
    $bindingProperties = @($golden.layoutProbeBindings.$probeBindingName.PSObject.Properties | ForEach-Object { $_.Name })
    if ($bindingProperties.Count -ne 2 -or $bindingProperties -notcontains 'set' -or $bindingProperties -notcontains 'binding')
    {
        throw ('ABI v0 layout-probe binding schema is invalid: ' + $probeBindingName)
    }
}
if (-not [bool]$golden.layoutProbeBindings.testOnly)
{
    throw 'ABI v0 layout-probe bindings must remain explicitly test-only.'
}
foreach ($expected in $expectedResources)
{
    $variableIds = @($typeNames.Keys | Where-Object { [string]$typeNames[$_] -eq $expected.Name })
    if ($variableIds.Count -ne 1)
    {
        throw ('Expected exactly one SPIR-V resource named ' + $expected.Name + ', found ' + $variableIds.Count + '.')
    }
    $variableId = [string]$variableIds[0]
    if (-not $descriptorSets.ContainsKey($variableId) -or -not $descriptorBindings.ContainsKey($variableId))
    {
        throw ('SPIR-V resource has incomplete descriptor decorations: ' + $expected.Name)
    }
    if ([int]$descriptorSets[$variableId] -ne [int]$expected.Set -or
        [int]$descriptorBindings[$variableId] -ne [int]$expected.Binding)
    {
        throw ('ABI v0 descriptor mismatch for ' + $expected.Name + ': golden=' + $expected.Set + ':' + $expected.Binding + ', SPIR-V=' + $descriptorSets[$variableId] + ':' + $descriptorBindings[$variableId])
    }
}

Write-Host ('ABI v0 golden matched SPIR-V: ' + $validatedRecordCount + ' records, 7 strides, ' + $expectedResources.Count + ' resources.')
