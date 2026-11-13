#pragma once

#include "openxr_dispatch.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
#include "dxvk_interop.hpp"
#include <wrl/client.h>
#include <cstdint>
#include <string>

// DXVK game device -> OpenXR over XR_KHR_vulkan_enable.
// DXVK cannot share D3D11 textures with another device on Windows, so the
// runtime's own D3D11 device cannot open our images. Instead the session is
// bound to DXVK's VkDevice/queue, the runtime creates Vulkan swapchain images
// on it, and DXVK wraps each VkImage as an ID3D11Texture2D (render target
// only, so DXVK keeps it in COLOR_ATTACHMENT_OPTIMAL as OpenXR requires).
// Every OpenXR call that may submit to the shared queue holds DXVK's
// submission lock.
namespace vr::openxr::vulkan
{
	struct binding
	{
		Microsoft::WRL::ComPtr<IDXGIVkInteropDevice1> interop;
		VkInstance instance{};
		VkPhysicalDevice physical{};
		VkDevice device{};
		VkQueue queue{};
		std::uint32_t queue_family{};
		std::uint32_t queue_index{};
		explicit operator bool() const noexcept { return interop != nullptr && device != VK_NULL_HANDLE; }
	};

	// The game device is DXVK's D3D11 implementation.
	[[nodiscard]] binding detect(ID3D11Device* device) noexcept;
	// Swapchain helpers and queue guards use this binding until deactivate().
	void activate(const binding& value, dispatch_table& dispatch) noexcept;
	void deactivate() noexcept;
	[[nodiscard]] IDXGIVkInteropDevice1* active() noexcept;

	[[nodiscard]] std::int64_t to_vk_format(std::int64_t dxgi) noexcept;
	[[nodiscard]] std::int64_t to_dxgi_format(std::int64_t vk) noexcept;

	// Session preflight: runtime physical device must be DXVK's.
	[[nodiscard]] bool check_device(const dispatch_table& dispatch, XrInstance instance, XrSystemId system,
		const binding& value, std::string& extensions, std::string& error, XrResult& result);

	// Wraps one runtime VkImage as a DXVK render-target texture.
	[[nodiscard]] bool wrap_image(VkImage image, std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
		Microsoft::WRL::ComPtr<ID3D11Texture2D>& output, std::string& error) noexcept;
}
#endif
