# Vulkan renderer (DXVK)

Optional workaround for AMD driver rendering faults: the game's Direct3D 11
runs on Vulkan through DXVK 3.1.1 (bundled, `overlord-mwr/dxvk/`, zlib license).

## Turning it on

Launcher > Basics > VR backend > **Vulkan renderer (DXVK)** (`vr_dxvk`, off by
default). When the game is started from the launcher, `launcher/dxvk_install.cpp`
copies the bundled `d3d11.dll`/`dxgi.dll` into the game folder; with the option
off it removes them again. Only byte-identical copies are replaced or removed;
a foreign `d3d11.dll`/`dxgi.dll` (ReShade, another DXVK) stops the launch with a
message instead. Direct `-singleplayer` starts keep whatever files are present.

The first start after switching shows the game's "hardware changed" settings
prompt, because DXVK reports its own adapter identity.

## How VR presentation works with DXVK

DXVK cannot share D3D11 textures with another device on Windows
(`OpenSharedResource` returns `E_INVALIDARG`, even DXVK to DXVK), so the
runtime's own D3D11 device cannot open our swapchain images. With a DXVK game
device the OpenXR backend therefore uses `XR_KHR_vulkan_enable`
(`component/vr/openxr_vulkan.cpp`):

- `IDXGIVkInteropDevice1` gives DXVK's `VkInstance`, `VkPhysicalDevice`,
  `VkDevice` and graphics queue; the session binds them
  (`XrGraphicsBindingVulkanKHR`). The runtime's `xrGetVulkanGraphicsDeviceKHR`
  must name the same GPU, otherwise initialization stops with a clear error.
- Swapchains use the VkFormat of the chosen DXGI format; every runtime `VkImage`
  is wrapped with `CreateTexture2DFromVkImage` as a render-target-only texture,
  so DXVK keeps it in `COLOR_ATTACHMENT_OPTIMAL`, the layout OpenXR uses.
  The rest of the D3D11 presentation path is unchanged.
- xrCreate/DestroySwapchain, xrAcquire/ReleaseSwapchainImage and
  xrBegin/EndFrame hold DXVK's submission-queue lock; release and end frame
  flush DXVK's recorded work first.

The OpenVR backend submits D3D11 textures to SteamVR and refuses a DXVK device.

## Verification

Mock only (no headset): mockxr gained `XR_KHR_vulkan_enable` (VkImage swapchains
on the app device, Vulkan readback for captures). Killhouse: transport
`dxvk_vulkan_openxr`, thousands of frames, 0 failures, both eyes correct; D3D11
path unchanged (`h2_device_openxr`). Real runtimes (Meta, SteamVR, Virtual
Desktop) and AMD hardware still need a headset check.
