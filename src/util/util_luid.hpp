#pragma once

#include <windows.h>
#include <cstdint>

namespace dxmt {

/**
 * \brief The LUID of a Metal device's GPU
 *
 * Wine gives each GPU a LUID of its own, which its D3DKMT, DXCore and Vulkan
 * (VkPhysicalDeviceIDProperties::deviceLUID) report. Every DXMT module
 * reports the same one, so that D3D12 devices and D3DKMT find an adapter by
 * its LUID. Without a Wine GPU to match, it is the big-endian registry ID,
 * as MoltenVK reports it.
 *
 * The result is cached for the process, per registry ID.
 *
 * \param [in] RegistryId The Metal device's registry ID
 * \param [in] pName The Metal device's name
 * \param [in] DeviceCount The number of Metal devices
 * \returns The LUID
 */
LUID GetMetalDeviceLuid(uint64_t RegistryId, const WCHAR *pName, uint32_t DeviceCount);

} // namespace dxmt
