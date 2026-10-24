#include <std_include.hpp>
#include "gunship_cabin.hpp"
#include "gameplay/view_aimed_weapon.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/image.hpp>
#include <utils/io.hpp>
#include <d3dcompiler.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace vr::gunship_cabin
{
	namespace
	{
		constexpr const char* asset_dir = "overlord-mwr/ac130_cabin/";
		struct vertex { float position[3], normal[3], uv[2]; };
		static_assert(sizeof(vertex) == 32);
		struct group { std::string texture; unsigned first{}, count{}; };
		struct image { std::string rgba; int width{}, height{}; };
		// Immutable CPU copy of data/ac130_cabin (see tools/build_ac130_cabin.py).
		struct asset
		{
			vec3 eye{}, forward{};
			std::vector<vertex> vertices;
			std::vector<std::uint32_t> indices;
			std::vector<group> groups;
			std::vector<image> images; // Parallel to groups; empty for the screen/untextured.
		};
		std::shared_ptr<const asset> loaded;
		std::mutex loaded_mutex;
		std::atomic_bool load_started{}, load_failed{};
		game::dvar_t* enabled{};

		std::shared_ptr<const asset> current_asset()
		{
			const std::lock_guard lock(loaded_mutex);
			return loaded;
		}
		std::shared_ptr<asset> parse(const std::string& data, std::string& error)
		{
			const auto read = [&](std::size_t offset, void* out, std::size_t size) {
				if (offset > data.size() || size > data.size() - offset) return false;
				std::memcpy(out, data.data() + offset, size);
				return true;
			};
			auto value = std::make_shared<asset>();
			std::uint32_t counts[3]{};
			if (data.size() < 40 || data.compare(0, 4, "OAC1") || !read(4, value->eye.data(), 12) ||
			    !read(16, value->forward.data(), 12) || !read(28, counts, sizeof(counts)) ||
			    !counts[0] || counts[0] > 1u << 22 || !counts[1] || counts[1] > 1u << 23 || !counts[2] || counts[2] > 64)
			{
				error = "cabin.bin header rejected";
				return {};
			}
			std::size_t offset = 40;
			for (unsigned i = 0; i < counts[2]; ++i, offset += 40)
			{
				char name[33]{};
				group g;
				if (!read(offset, name, 32) || !read(offset + 32, &g.first, 4) || !read(offset + 36, &g.count, 4) ||
				    g.first > counts[1] || g.count > counts[1] - g.first || g.count % 3)
				{
					error = "cabin.bin group rejected";
					return {};
				}
				g.texture = name;
				value->groups.push_back(std::move(g));
			}
			value->vertices.resize(counts[0]);
			value->indices.resize(counts[1]);
			if (!read(offset, value->vertices.data(), counts[0] * sizeof(vertex)) ||
			    !read(offset + counts[0] * sizeof(vertex), value->indices.data(), counts[1] * 4u))
			{
				error = "cabin.bin geometry truncated";
				return {};
			}
			for (const auto index : value->indices)
				if (index >= counts[0])
				{
					error = "cabin.bin index out of range";
					return {};
				}
			for (const auto& g : value->groups)
			{
				image decoded;
				// "@color:RRGGBB" (lit) and "@emit:RRGGBB" (unlit) are solid procedural surfaces.
				const auto hex = g.texture.rfind("@color:", 0) == 0 ? g.texture.substr(7) :
				                 g.texture.rfind("@emit:", 0) == 0 ? g.texture.substr(6) : std::string{};
				if (!hex.empty())
				{
					const auto rgb = std::strtoul(hex.c_str(), nullptr, 16);
					const unsigned char pixel[4]{static_cast<unsigned char>(rgb >> 16 & 0xff), static_cast<unsigned char>(rgb >> 8 & 0xff), static_cast<unsigned char>(rgb & 0xff), 0xff};
					decoded = {std::string(reinterpret_cast<const char*>(pixel), 4), 1, 1};
				}
				else if (!g.texture.empty() && g.texture != "@screen")
				{
					std::string file;
					if (g.texture.find_first_of("/\\") != std::string::npos ||
					    !utils::io::read_file(asset_dir + g.texture, &file))
					{
						error = "cabin texture missing: " + g.texture;
						return {};
					}
					const utils::image png(file);
					if (png.get_width() <= 0 || png.get_height() <= 0 || png.get_width() > 8192 || png.get_height() > 8192)
					{
						error = "cabin texture undecodable: " + g.texture;
						return {};
					}
					decoded = {png.get_data(), png.get_width(), png.get_height()};
				}
				value->images.push_back(std::move(decoded));
			}
			return value;
		}
		void begin_load()
		{
			if (load_started.exchange(true)) return;
			scheduler::once([] {
				std::string data, error;
				std::shared_ptr<asset> value;
				if (!utils::io::read_file(std::string(asset_dir) + "cabin.bin", &data))
					error = "cabin.bin missing";
				else
					value = parse(data, error);
				if (!value)
				{
					load_failed = true;
					console::error("[VR gunship cabin] %s; the flat theater is used instead\n", error.c_str());
					return;
				}
				const std::lock_guard lock(loaded_mutex);
				loaded = std::move(value);
			}, scheduler::pipeline::async);
		}

		using matrix = std::array<float, 16>; // Row-major, column vectors (clip = M * p).
		matrix multiply(const matrix& a, const matrix& b) noexcept
		{
			matrix out{};
			for (unsigned r = 0; r < 4; ++r)
				for (unsigned c = 0; c < 4; ++c)
					for (unsigned k = 0; k < 4; ++k) out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
			return out;
		}
		std::array<float, 9> rotation(const quat& q) noexcept
		{
			const float x = q[0], y = q[1], z = q[2], w = q[3];
			return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
			        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
			        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
		}
		constexpr char shader[] = R"(
cbuffer Frame : register(b0) { row_major float4x4 clipFromModel; float4 options; };
Texture2D<float4> albedo : register(t0);
SamplerState linearWrap : register(s0);
struct VI { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VO { float4 position : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
VO vs(VI i) { VO o; o.position = mul(clipFromModel, float4(i.position, 1)); o.normal = i.normal; o.uv = i.uv; return o; }
float3 encode(float3 linearColor) { return options.x > 0 ? pow(saturate(linearColor), 1 / 2.2) : linearColor; }
float4 lit(VO i) : SV_Target {
    // Dim cabin light from the overhead strip; the station screens are the key light.
    float3 n = normalize(i.normal);
    float light = 0.20 + 0.40 * saturate(abs(dot(n, normalize(float3(0.25, 0.9, -0.35)))));
    return float4(encode(albedo.Sample(linearWrap, i.uv).rgb * light * options.z), 1);
}
float4 emissive(VO i) : SV_Target { return float4(encode(albedo.Sample(linearWrap, i.uv).rgb * options.y), 1); }
float4 screen(VO i) : SV_Target {
    if (any(i.uv < 0) || any(i.uv > 1)) return float4(0, 0, 0, 1);
    float3 c = albedo.Sample(linearWrap, i.uv).rgb;
    // The native frame is display-encoded; linearize unless the target stores encoded values.
    return float4(options.x > 0 ? c : pow(c, 2.2), 1);
}
)";
		struct frame_constants { matrix transform; std::array<float, 4> options; };
		static_assert(sizeof(frame_constants) == 80);
	}

	bool requested() noexcept
	{
		if (!enabled || !enabled->current.enabled || load_failed.load()) return false;
		if (!gameplay::view_aimed::active()) return false;
		begin_load();
		return bool(current_asset());
	}

	anchor make_anchor(const vec3& head, const quat& orientation) noexcept
	{
		const auto r = rotation(orientation);
		const float fx = -r[2], fz = -r[8]; // Head forward is -z.
		const auto model = current_asset();
		if (!model || !std::isfinite(fx) || !std::isfinite(fz) || std::abs(fx) + std::abs(fz) < 1e-3f) return {};
		const float yaw = std::atan2(fx, fz) - std::atan2(model->forward[0], model->forward[2]);
		return {head, yaw, std::isfinite(head[0]) && std::isfinite(head[1]) && std::isfinite(head[2])};
	}

	struct renderer::resources
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		std::shared_ptr<const asset> source;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> lit, screen, emissive;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices, constants;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view;
		unsigned depth_width{}, depth_height{};
		std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white;
		ID3D11Texture2D* screen_texture{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> screen_view;

		bool create(ID3D11Device* owner, const std::shared_ptr<const asset>& model, std::string& error)
		{
			device = owner;
			source = model;
			Microsoft::WRL::ComPtr<ID3DBlob> vs_code, lit_code, screen_code, emissive_code, diagnostics;
			const auto compile = [&](const char* entry, const char* profile, Microsoft::WRL::ComPtr<ID3DBlob>& out) {
				return SUCCEEDED(D3DCompile(shader, sizeof(shader) - 1, "gunship-cabin", nullptr, nullptr, entry, profile,
				                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &diagnostics));
			};
			if (!compile("vs", "vs_5_0", vs_code) || !compile("lit", "ps_5_0", lit_code) || !compile("screen", "ps_5_0", screen_code) || !compile("emissive", "ps_5_0", emissive_code) ||
			    FAILED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs)) ||
			    FAILED(device->CreatePixelShader(lit_code->GetBufferPointer(), lit_code->GetBufferSize(), nullptr, &lit)) ||
			    FAILED(device->CreatePixelShader(screen_code->GetBufferPointer(), screen_code->GetBufferSize(), nullptr, &screen)) ||
			    FAILED(device->CreatePixelShader(emissive_code->GetBufferPointer(), emissive_code->GetBufferSize(), nullptr, &emissive)))
			{
				error = "cabin shaders rejected";
				return false;
			}
			const D3D11_INPUT_ELEMENT_DESC elements[]{
			    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
			    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
			if (FAILED(device->CreateInputLayout(elements, 3, vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &layout)))
			{
				error = "cabin input layout rejected";
				return false;
			}
			D3D11_BUFFER_DESC desc{};
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.ByteWidth = static_cast<UINT>(model->vertices.size() * sizeof(vertex));
			D3D11_SUBRESOURCE_DATA data{model->vertices.data()};
			if (FAILED(device->CreateBuffer(&desc, &data, &vertices))) { error = "cabin vertex buffer failed"; return false; }
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			desc.ByteWidth = static_cast<UINT>(model->indices.size() * 4);
			data.pSysMem = model->indices.data();
			if (FAILED(device->CreateBuffer(&desc, &data, &indices))) { error = "cabin index buffer failed"; return false; }
			D3D11_BUFFER_DESC cb{};
			cb.ByteWidth = sizeof(frame_constants);
			cb.Usage = D3D11_USAGE_DYNAMIC;
			cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&cb, nullptr, &constants))) { error = "cabin constants failed"; return false; }
			D3D11_SAMPLER_DESC sampler_desc{};
			sampler_desc.Filter = D3D11_FILTER_ANISOTROPIC;
			sampler_desc.MaxAnisotropy = 8;
			sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_RASTERIZER_DESC raster_desc{};
			raster_desc.FillMode = D3D11_FILL_SOLID;
			raster_desc.CullMode = D3D11_CULL_NONE; // The FlightGear mesh is not consistently wound.
			raster_desc.DepthClipEnable = TRUE;
			D3D11_DEPTH_STENCIL_DESC depth_desc{};
			depth_desc.DepthEnable = TRUE;
			depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth_desc.DepthFunc = D3D11_COMPARISON_LESS;
			if (FAILED(device->CreateSamplerState(&sampler_desc, &sampler)) || FAILED(device->CreateRasterizerState(&raster_desc, &raster)) ||
			    FAILED(device->CreateDepthStencilState(&depth_desc, &depth_state)))
			{
				error = "cabin pipeline state failed";
				return false;
			}
			const auto upload = [&](const void* rgba, int width, int height, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out) {
				// Albedo is sRGB; a full mip chain keeps distant consoles stable in the headset.
				D3D11_TEXTURE2D_DESC t{};
				t.Width = width;
				t.Height = height;
				t.MipLevels = 0;
				t.ArraySize = 1;
				t.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				t.SampleDesc.Count = 1;
				t.Usage = D3D11_USAGE_DEFAULT;
				t.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
				t.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				if (FAILED(device->CreateTexture2D(&t, nullptr, &texture)) || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &out)))
					return false;
				Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
				device->GetImmediateContext(&immediate);
				immediate->UpdateSubresource(texture.Get(), 0, nullptr, rgba, width * 4, 0);
				immediate->GenerateMips(out.Get());
				return true;
			};
			const std::uint32_t white_pixel = 0xffffffff;
			if (!upload(&white_pixel, 1, 1, white)) { error = "cabin white texture failed"; return false; }
			for (const auto& image : model->images)
			{
				textures.emplace_back();
				if (!image.rgba.empty() && !upload(image.rgba.data(), image.width, image.height, textures.back()))
				{
					error = "cabin texture upload failed";
					return false;
				}
			}
			return true;
		}
		bool ensure_depth(unsigned width, unsigned height)
		{
			if (depth_view && depth_width == width && depth_height == height) return true;
			depth_view.Reset();
			depth.Reset();
			D3D11_TEXTURE2D_DESC t{};
			t.Width = width;
			t.Height = height;
			t.MipLevels = 1;
			t.ArraySize = 1;
			t.Format = DXGI_FORMAT_D32_FLOAT;
			t.SampleDesc.Count = 1;
			t.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			if (FAILED(device->CreateTexture2D(&t, nullptr, &depth)) || FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &depth_view)))
				return false;
			depth_width = width;
			depth_height = height;
			return true;
		}
		bool ensure_screen(ID3D11Texture2D* texture)
		{
			if (!texture) { screen_view.Reset(); screen_texture = nullptr; return false; }
			if (texture == screen_texture && screen_view) return true;
			screen_view.Reset();
			D3D11_TEXTURE2D_DESC t{};
			texture->GetDesc(&t);
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = t.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM :
			              t.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM :
			              t.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS ? DXGI_FORMAT_R10G10B10A2_UNORM : t.Format;
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(texture, &view, &screen_view))) return false;
			screen_texture = texture;
			return true;
		}
	};

	bool renderer::prepare(ID3D11Device* device, std::string& error) noexcept
	{
		try
		{
			const auto model = current_asset();
			if (!device || !model) { error = "cabin asset unavailable"; return false; }
			if (state_ && state_->device.Get() == device && state_->source == model) return true;
			auto fresh = std::make_shared<resources>();
			if (!fresh->create(device, model, error))
			{
				// Do not rebuild every frame; the flat theater remains available.
				load_failed = true;
				console::error("[VR gunship cabin] %s; the flat theater is used instead\n", error.c_str());
				return false;
			}
			state_ = std::move(fresh);
			return true;
		}
		catch (...)
		{
			error = "cabin preparation exception";
			return false;
		}
	}

	bool renderer::render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
	                      unsigned width, unsigned height, const eye_view& eye, const anchor& seat,
	                      ID3D11Texture2D* screen, std::string& error) noexcept
	{
		try
		{
			const auto model = current_asset();
			if (!device || !context || !target || !width || !height || !model || !seat.valid)
			{
				error = "cabin render inputs unavailable";
				return false;
			}
			if (!prepare(device, error)) return false;
			auto& s = *state_;
			if (!s.ensure_depth(width, height)) { error = "cabin depth target failed"; return false; }
			const bool has_screen = s.ensure_screen(screen);

			// clip <- projection <- view (inverse eye pose) <- seat placement <- model.
			const float c = std::cos(seat.yaw), si = std::sin(seat.yaw);
			// Rotation about +y taking atan2(x, z) angle phi to phi + yaw.
			const matrix place{c, 0, si, 0, 0, 1, 0, 0, -si, 0, c, 0, 0, 0, 0, 1};
			matrix translate_eye{1, 0, 0, -model->eye[0], 0, 1, 0, -model->eye[1], 0, 0, 1, -model->eye[2], 0, 0, 0, 1};
			matrix seat_world = multiply(place, translate_eye);
			seat_world[3] += seat.head[0];
			seat_world[7] += seat.head[1];
			seat_world[11] += seat.head[2];
			const auto r = rotation(eye.orientation);
			matrix view{r[0], r[3], r[6], 0, r[1], r[4], r[7], 0, r[2], r[5], r[8], 0, 0, 0, 0, 1};
			for (unsigned row = 0; row < 3; ++row)
				view[row * 4 + 3] = -(view[row * 4] * eye.position[0] + view[row * 4 + 1] * eye.position[1] + view[row * 4 + 2] * eye.position[2]);
			const float near_plane = .03f, far_plane = 80.f;
			const float sx = eye.tan_right - eye.tan_left, sy = eye.tan_up - eye.tan_down;
			if (!(sx > 0) || !(sy > 0)) { error = "cabin eye frustum invalid"; return false; }
			const float a = -far_plane / (far_plane - near_plane), b = -far_plane * near_plane / (far_plane - near_plane);
			const matrix projection{2 / sx, 0, (eye.tan_right + eye.tan_left) / sx, 0,
			                        0, 2 / sy, (eye.tan_up + eye.tan_down) / sy, 0,
			                        0, 0, a, b,
			                        0, 0, -1, 0};
			frame_constants frame{multiply(projection, multiply(view, seat_world)), {}};
			D3D11_RENDER_TARGET_VIEW_DESC target_desc{};
			target->GetDesc(&target_desc);
			const bool srgb_target = target_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || target_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			frame.options = {srgb_target ? 0.f : 1.f, .85f, 1.f, 0.f}; // y: light strip level
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(s.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { error = "cabin constants map failed"; return false; }
			std::memcpy(mapped.pData, &frame, sizeof(frame));
			context->Unmap(s.constants.Get(), 0);

			const float black[]{0, 0, 0, 1};
			context->ClearRenderTargetView(target, black);
			context->ClearDepthStencilView(s.depth_view.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
			const D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
			context->RSSetViewports(1, &viewport);
			context->RSSetState(s.raster.Get());
			context->OMSetRenderTargets(1, &target, s.depth_view.Get());
			context->OMSetDepthStencilState(s.depth_state.Get(), 0);
			context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
			const UINT stride = sizeof(vertex), offset = 0;
			context->IASetVertexBuffers(0, 1, s.vertices.GetAddressOf(), &stride, &offset);
			context->IASetIndexBuffer(s.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
			context->IASetInputLayout(s.layout.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(s.vs.Get(), nullptr, 0);
			context->VSSetConstantBuffers(0, 1, s.constants.GetAddressOf());
			context->PSSetConstantBuffers(0, 1, s.constants.GetAddressOf());
			context->PSSetSamplers(0, 1, s.sampler.GetAddressOf());
			for (std::size_t i = 0; i < model->groups.size(); ++i)
			{
				const auto& g = model->groups[i];
				const bool is_screen = g.texture == "@screen";
				ID3D11ShaderResourceView* view_srv = is_screen ? (has_screen ? s.screen_view.Get() : s.white.Get()) :
				                                     s.textures[i] ? s.textures[i].Get() : s.white.Get();
				const bool is_emissive = g.texture.rfind("@emit:", 0) == 0;
				context->PSSetShader(is_screen && has_screen ? s.screen.Get() : is_emissive ? s.emissive.Get() : s.lit.Get(), nullptr, 0);
				context->PSSetShaderResources(0, 1, &view_srv);
				context->DrawIndexed(g.count, g.first, 0);
			}
			ID3D11ShaderResourceView* none{};
			context->PSSetShaderResources(0, 1, &none);
			context->OMSetRenderTargets(0, nullptr, nullptr);
			return true;
		}
		catch (...)
		{
			error = "cabin render exception";
			return false;
		}
	}

	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_gunshipCabin", true, game::DVAR_FLAG_SAVED,
			                               "Play the AC-130 at a fire-control station in VR (off: flat theater screen)");
		}
	};
}
REGISTER_COMPONENT(vr::gunship_cabin::component)
