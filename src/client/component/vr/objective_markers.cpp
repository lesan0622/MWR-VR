#include <std_include.hpp>
#include "directional_ui.hpp"
#include "native_waypoints.hpp"
#include "engine_stereo_bridge.hpp"
#include "head_pose_bridge.hpp"
#include "settings.hpp"
#include "game/dvars.hpp"
#include "component/scheduler.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/native_memory.hpp>

// MWR has no 3D objective waypoint; the native compass shows the current
// objective as a star. In VR the current objective is marked in the world
// instead: its icon and distance at the objective while it is in view, or at
// the edge of a head-relative canvas pointing toward it otherwise.
namespace vr::objective_markers
{
	namespace
	{
		using vec3 = directional_ui::vec3;
		// objective_add/objective_position (0x1402E5DB0/0x1402E6BC0) write each
		// objective as a server info config string at 11+n, n<36, with dvar-hashed
		// keys ("0x%08X"): state, str, org0..org7, etNum0.
		constexpr std::uintptr_t config_string = 0x1401A2F10;
		constexpr int first_objective = 0xb, objective_count = 36;
		constexpr int state_active = 1, state_current = 4; // OBJST_ACTIVE / OBJST_CURRENT
		constexpr std::uint32_t hash(std::string_view text) noexcept
		{
			std::uint32_t value = 0x319712C3;
			for (std::size_t i = 0; i <= text.size(); ++i)
			{
				auto c = i < text.size() ? static_cast<unsigned char>(text[i]) : 0u;
				if (c >= 'A' && c <= 'Z') c += 32;
				value = (c ^ value) * 0xB3CB2E29u;
			}
			return value;
		}
		static_assert(hash("org0") == 0xEB4AD191 && hash("state") == 0xA4A65884 && hash("str") == 0x512D90F6);
		game::dvar_t* enabled{};
		std::atomic_uint64_t frames{}, objectives{}, drawn{}, rejected{};
		std::optional<std::string_view> value(std::string_view info, std::uint32_t key) noexcept
		{
			char name[11]{};
			std::snprintf(name, sizeof(name), "0x%08X", key);
			// Info strings alternate "\key\value"; keys never contain backslashes.
			std::size_t at = 0;
			while (at < info.size() && info[at] == '\\')
			{
				const auto key_end = info.find('\\', at + 1);
				if (key_end == info.npos) return {};
				const auto value_end = std::min(info.find('\\', key_end + 1), info.size());
				if (info.substr(at + 1, key_end - at - 1) == name)
					return info.substr(key_end + 1, value_end - key_end - 1);
				at = value_end;
			}
			return {};
		}
		bool parse_vec(std::string_view text, vec3& out) noexcept
		{
			std::array<char, 96> buffer{};
			if (text.empty() || text.size() >= buffer.size()) return false;
			std::copy(text.begin(), text.end(), buffer.begin());
			if (sscanf_s(buffer.data(), "%f %f %f", &out[0], &out[1], &out[2]) != 3) return false;
			return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]) &&
			       (out[0] != 0 || out[1] != 0 || out[2] != 0);
		}
		std::vector<vec3> objectives_in_state(int wanted);
		// Current objectives; levels that only add "active" ones (sniperescape) show those.
		std::vector<vec3> current_objectives()
		{
			auto result = objectives_in_state(state_current);
			return result.empty() ? objectives_in_state(state_active) : result;
		}
		std::vector<vec3> objectives_in_state(int wanted)
		{
			std::vector<vec3> result;
			for (int i = 0; i < objective_count; ++i)
			{
				const auto* raw = utils::hook::invoke<const char*>(config_string, first_objective + i);
				if (!raw || !*raw) continue;
				const std::string_view info(raw, strnlen(raw, 1024));
				const auto state = value(info, hash("state"));
				if (!state || *state != std::to_string(wanted)) continue;
				// An objective on an entity follows that entity.
				if (const auto entity = value(info, hash("etNum0")); entity && !entity->empty())
				{
					int number{};
					const std::string text(*entity);
					if (sscanf_s(text.c_str(), "%d", &number) == 1 && number > 0 && number < 2048)
					{
						vec3 origin{};
						if (utils::native_memory::read_bytes(origin.data(), &game::g_entities[number].origin, sizeof(origin)) &&
						    std::isfinite(origin[0]) && (origin[0] != 0 || origin[1] != 0 || origin[2] != 0))
						{
							result.push_back(origin);
							continue;
						}
					}
				}
				for (int k = 0; k < 8; ++k)
				{
					vec3 position{};
					const auto key = "org" + std::to_string(k);
					if (const auto text = value(info, hash(key)); text && parse_vec(*text, position))
						result.push_back(position);
				}
			}
			return result;
		}
		vec3 cross(const vec3& a, const vec3& b) noexcept
		{
			return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
		}
		float dot(const vec3& a, const vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		game::Material* icon()
		{
			const auto header = game::DB_FindXAssetHeader(game::ASSET_TYPE_MATERIAL, "objective", 0);
			return header.material;
		}
		void draw()
		{
			++frames;
			if (!enabled || !enabled->current.enabled) return;
			if (!game::CL_IsCgameInitialized() || !engine_stereo_bridge::is_active() || *game::keyCatchers) return;
			head_pose_bridge::spatial_frame head;
			if (!head_pose_bridge::get_spatial_frame(head) || !(head.units_per_meter > 0)) return;
			const auto* placement = game::ScrPlace_GetViewPlacement();
			if (!placement) return;
			const auto positions = current_objectives();
			objectives = positions.size();
			if (positions.empty()) return;
			auto* font = game::R_RegisterFont("fonts/defaultBold.otf", 27);
			auto* material = icon();
			const vec3 eye{head.head_position[0], head.head_position[1], head.head_position[2]};
			const vec3 forward{head.head_forward[0], head.head_forward[1], head.head_forward[2]};
			const vec3 up{head.head_up[0], head.head_up[1], head.head_up[2]};
			const auto right = cross(forward, up);
			for (const auto& position : positions)
			{
				vec3 delta{};
				for (unsigned i = 0; i < 3; ++i) delta[i] = position[i] - eye[i];
				const float meters = std::sqrt(dot(delta, delta)) / head.units_per_meter;
				if (!std::isfinite(meters) || meters < 1.5f) continue; // Arrived.
				directional_ui::waypoint marker;
				marker.world = position;
				std::copy_n(placement->realViewportSize, 2, marker.viewport.begin());
				const float width = marker.viewport[0], height = marker.viewport[1];
				if (width < 16 || height < 16) return;
				// The same 80-degree canvas as native clamped indicators.
				const float tangent_x = .839099631f, tangent_y = tangent_x * height / width;
				marker.tangent = {tangent_x, tangent_y};
				const float depth = dot(delta, forward);
				const float x = dot(delta, right), y = dot(delta, up);
				const bool in_view = depth > 0 && std::abs(x / depth) < tangent_x * .85f && std::abs(y / depth) < tangent_y * .85f;
				std::array<float, 2> center{width * .5f, height * .5f};
				if (!in_view)
				{
					// Edge of the head-relative canvas, toward the objective.
					const float length = std::sqrt(x * x + y * y);
					const float nx = length > 0 ? x / length : 0, ny = length > 0 ? y / length : -1;
					center = {width * (.5f + nx * .38f), height * (.5f - ny * .38f)};
					marker.clamped = true;
				}
				marker.anchor = {width * .5f, height * .5f};
				const float size = 52;
				char distance[16]{};
				std::snprintf(distance, sizeof(distance), "%dm", static_cast<int>(std::lround(meters)));
				const float text_scale = .8f;
				const float text_width = font ? game::R_TextWidth(distance, 16, font) * text_scale : 0;
				const directional_ui::rectangle bounds{
				    std::min(center[0] - size * .5f, center[0] - text_width * .5f) - 4, center[1] - size * .5f - 4,
				    std::max(center[0] + size * .5f, center[0] + text_width * .5f) + 4, center[1] + size * .5f + 36};
				const bool accepted = native_waypoints::draw_world_marker(marker, [&]
				{
					float white[]{1, 1, 1, 1}, gold[]{1, .85f, .25f, 1};
					if (material)
						game::R_AddCmdDrawStretchPic(center[0] - size * .5f, center[1] - size * .5f, size, size, 0, 0, 1, 1, gold, material);
					if (font)
						game::R_AddCmdDrawText(distance, 16, font, center[0] - text_width * .5f, center[1] + size * .5f + 26,
						                       text_scale, text_scale, 0, white, 4);
				}, bounds);
				if (accepted) ++drawn; else ++rejected;
			}
		}
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool(settings::objective_markers.name, settings::objective_markers.default_value,
			                               game::DVAR_FLAG_SAVED, "Mark the current objective in the VR world");
			scheduler::loop(draw, scheduler::pipeline::lui);
			command::add("vr_objective_status", []
			{
				console::info("[VR objectives] frames=%llu current=%llu drawn=%llu rejected=%llu\n",
				              frames.load(), objectives.load(), drawn.load(), rejected.load());
			});
		}
	};
}
REGISTER_COMPONENT(vr::objective_markers::component)
