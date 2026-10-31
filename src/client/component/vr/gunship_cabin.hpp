#pragma once
#include <array>
#include <d3d11.h>
#include <memory>
#include <string>
#include <wrl/client.h>

// MWR AC-130: the player sits at the fire-control officer's station of an
// AC-130 battle management center (FlightGear AC-130 by helijah, armament
// station by Jack Mermod; GPL, data/ac130_cabin). The native flat gunship frame,
// with its own HUD, is shown on that station's screen.
namespace vr::gunship_cabin
{
	using vec3 = std::array<float, 3>;
	using quat = std::array<float, 4>; // x, y, z, w
	// The native weapon is an AC-130 gun and vr_gunshipCabin is on.
	bool requested() noexcept;
	// Model seat placed at the head pose sampled when the cabin opened.
	struct anchor
	{
		vec3 head{};
		float yaw{}; // Radians about +y, taking the model's forward to the head's.
		bool valid{};
	};
	anchor make_anchor(const vec3& head, const quat& orientation) noexcept;
	struct eye_view
	{
		vec3 position{};
		quat orientation{};
		float tan_left{}, tan_right{}, tan_up{}, tan_down{};
	};
	class renderer
	{
		struct resources;
		std::shared_ptr<resources> state_;

	  public:
		// Creates the device resources once; a failure keeps the flat theater.
		bool prepare(ID3D11Device* device, std::string& error) noexcept;
		// Draws into `target` with its own depth; `screen` is the copied native frame.
		bool render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
		            unsigned width, unsigned height, const eye_view& eye, const anchor& seat,
		            ID3D11Texture2D* screen, std::string& error) noexcept;
	};
}
