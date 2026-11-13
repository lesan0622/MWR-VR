#include <std_include.hpp>

#include "openxr_vulkan.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
#include "component/d3d11.hpp"

#include <atomic>
#include <format>

namespace vr::openxr::vulkan
{
	namespace
	{
		Microsoft::WRL::ComPtr<IDXGIVkInteropDevice1> current;
		std::atomic<IDXGIVkInteropDevice1*> guard{};

		PFN_xrCreateSwapchain original_create_swapchain{};
		PFN_xrDestroySwapchain original_destroy_swapchain{};
		PFN_xrAcquireSwapchainImage original_acquire{};
		PFN_xrReleaseSwapchainImage original_release{};
		PFN_xrBeginFrame original_begin_frame{};
		PFN_xrEndFrame original_end_frame{};

		// DXVK must not submit while the runtime uses the shared VkQueue.
		// No D3D11 call may run on this thread while the lock is held.
		class queue_lock
		{
		public:
			explicit queue_lock(const bool flush) noexcept : interop_(guard.load(std::memory_order_acquire))
			{
				if (!interop_) return;
				if (flush)
				{
					// Recorded D3D11 work on the images must reach the queue first.
					const auto queue = d3d11::acquire_gpu_queue_interop();
					interop_->FlushRenderingCommands();
				}
				interop_->LockSubmissionQueue();
			}
			~queue_lock() { if (interop_) interop_->ReleaseSubmissionQueue(); }
			queue_lock(const queue_lock&) = delete;
			queue_lock& operator=(const queue_lock&) = delete;
		private:
			IDXGIVkInteropDevice1* interop_;
		};

		XrResult XRAPI_CALL create_swapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* out)
		{ queue_lock lock(false); return original_create_swapchain(session, info, out); }
		XrResult XRAPI_CALL destroy_swapchain(XrSwapchain swapchain)
		{ queue_lock lock(true); return original_destroy_swapchain(swapchain); }
		XrResult XRAPI_CALL acquire(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* info, std::uint32_t* index)
		{ queue_lock lock(false); return original_acquire(swapchain, info, index); }
		XrResult XRAPI_CALL release(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info)
		{ queue_lock lock(true); return original_release(swapchain, info); }
		XrResult XRAPI_CALL begin_frame(XrSession session, const XrFrameBeginInfo* info)
		{ queue_lock lock(false); return original_begin_frame(session, info); }
		XrResult XRAPI_CALL end_frame(XrSession session, const XrFrameEndInfo* info)
		{ queue_lock lock(true); return original_end_frame(session, info); }

		struct format_pair { std::int64_t dxgi, vk; };
		constexpr format_pair formats[]{
		    {DXGI_FORMAT_R16G16B16A16_FLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
		    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, VK_FORMAT_R8G8B8A8_SRGB},
		    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, VK_FORMAT_B8G8R8A8_SRGB},
		    {DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
		    {DXGI_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
		};
	}

	binding detect(ID3D11Device* const device) noexcept
	{
		binding out;
		if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&out.interop))) || !out.interop) return {};
		out.interop->GetVulkanHandles(&out.instance, &out.physical, &out.device);
		out.interop->GetSubmissionQueue1(&out.queue, &out.queue_index, &out.queue_family);
		if (!out.instance || !out.physical || !out.device || !out.queue) return {};
		return out;
	}

	void activate(const binding& value, dispatch_table& dispatch) noexcept
	{
		current = value.interop;
		original_create_swapchain = dispatch.create_swapchain;
		original_destroy_swapchain = dispatch.destroy_swapchain;
		original_acquire = dispatch.acquire_swapchain_image;
		original_release = dispatch.release_swapchain_image;
		original_begin_frame = dispatch.begin_frame;
		original_end_frame = dispatch.end_frame;
		dispatch.create_swapchain = create_swapchain;
		dispatch.destroy_swapchain = destroy_swapchain;
		dispatch.acquire_swapchain_image = acquire;
		dispatch.release_swapchain_image = release;
		dispatch.begin_frame = begin_frame;
		dispatch.end_frame = end_frame;
		guard.store(current.Get(), std::memory_order_release);
	}

	void deactivate() noexcept
	{
		guard.store(nullptr, std::memory_order_release);
		current.Reset();
	}

	IDXGIVkInteropDevice1* active() noexcept { return guard.load(std::memory_order_acquire); }

	std::int64_t to_vk_format(const std::int64_t dxgi) noexcept
	{
		for (const auto& f : formats) if (f.dxgi == dxgi) return f.vk;
		return 0;
	}

	std::int64_t to_dxgi_format(const std::int64_t vk) noexcept
	{
		for (const auto& f : formats) if (f.vk == vk) return f.dxgi;
		return 0;
	}

	bool check_device(const dispatch_table& dispatch, const XrInstance instance, const XrSystemId system,
		const binding& value, std::string& extensions, std::string& error, XrResult& result)
	{
		XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
		result = dispatch.get_vulkan_graphics_requirements(instance, system, &requirements);
		if (XR_FAILED(result))
		{
			error = "xrGetVulkanGraphicsRequirementsKHR";
			return false;
		}
		VkPhysicalDevice runtime_device{};
		result = dispatch.get_vulkan_graphics_device(instance, system, value.instance, &runtime_device);
		if (XR_FAILED(result))
		{
			error = "xrGetVulkanGraphicsDeviceKHR";
			return false;
		}
		if (runtime_device != value.physical)
		{
			result = XR_ERROR_GRAPHICS_DEVICE_INVALID;
			error = "OpenXR runtime GPU differs from the DXVK GPU (set DXVK_FILTER_DEVICE_NAME or the "
			        "headset GPU in Windows graphics settings)";
			return false;
		}
		// Informational: DXVK enables external memory/semaphore/keyed mutex
		// itself; a runtime needing more fails later in xrCreateSession.
		std::uint32_t size{};
		extensions.clear();
		if (XR_SUCCEEDED(dispatch.get_vulkan_device_extensions(instance, system, 0, &size, nullptr)) && size)
		{
			extensions.resize(size);
			if (XR_FAILED(dispatch.get_vulkan_device_extensions(instance, system, size, &size, extensions.data())))
				extensions.clear();
			else if (!extensions.empty() && extensions.back() == '\0')
				extensions.pop_back();
		}
		result = XR_SUCCESS;
		return true;
	}

	bool wrap_image(const VkImage image, const std::uint32_t width, const std::uint32_t height,
		const DXGI_FORMAT format, Microsoft::WRL::ComPtr<ID3D11Texture2D>& output, std::string& error) noexcept
	{
		auto* const interop = active();
		if (!interop || !image)
		{
			error = "DXVK interop is unavailable for an OpenXR Vulkan image";
			return false;
		}
		// Render-target only: DXVK's resting layout is then
		// COLOR_ATTACHMENT_OPTIMAL, the layout OpenXR hands over and expects back.
		D3D11_TEXTURE2D_DESC1 desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc = {1, 0};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
		const auto hr = interop->CreateTexture2DFromVkImage(&desc, image, output.ReleaseAndGetAddressOf());
		if (FAILED(hr) || !output)
		{
			error = std::format("DXVK CreateTexture2DFromVkImage failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(hr));
			return false;
		}
		return true;
	}
}
#endif
