#pragma once

// abi-v2 is append-only over abi-v1 and freezes the Wave 3 reconstruction
// boundary without publishing any abi-v3 Reservoir state.
#include "contracts/AbiV1.hpp"
#include "contracts/AbiVersionV2.hpp"
#include "contracts/DescriptorRegistryV2.hpp"
#include "contracts/ReconstructionAbiV2.hpp"
