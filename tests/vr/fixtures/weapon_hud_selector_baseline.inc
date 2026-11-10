		bool baseline_select(const void* commands, scope& result, unsigned ordinal, unsigned expected,
			std::span<const native_waypoints::group> narrative_groups) noexcept
		{
			// Inspect the bounded immutable owner stream twice: first the native
			// ammo panel's material anchor, then its contained glyph/quad commands.
			bool found{};
			for (int pass = 0; pass < 2; ++pass)
			{
				unsigned borders{};
				const void* pending_blur{};
				auto cursor = static_cast<const std::byte*>(commands);
				bool terminated{};
				for (int n = 0; n < 2048; ++n)
				{
					std::array<std::byte, 4> header{};
					if (!utils::native_memory::read_bytes(header.data(), cursor, header.size())) return false;
					const auto size = field<std::uint16_t>(header.data(), 0);
					const auto op = field<std::uint8_t>(header.data(), 2);
					const auto flags = field<std::uint8_t>(header.data(), 3);
					if (op == 0) { terminated = true; break; }
					if (size < 4 || cursor - static_cast<const std::byte*>(commands) + size > 0x100000) return false;
					bool selected{};
					if ((flags == 0 || flags == 0x40) && op == 17 && size == 104)
					{
						std::array<std::byte, 104> quad{};
						std::array<char, 96> name_storage{};
						if (!utils::native_memory::read_bytes(quad.data(), cursor, quad.size())) return false;
						const auto name = material_name(quad.data(), name_storage);
						rectangle rect{};
						if(pass==0 && flags==0x40 && name=="h1_hud_weapwidget_blur")pending_blur=cursor;
						if (flags==0 && gameplay::weapon_hud::native::border(name) && pass == 0)
						{
							const auto* blur=pending_blur;pending_blur=nullptr;
							if (!(field<std::uint32_t>(quad.data(),96)>>24)) {cursor+=size;continue;}
							if (borders++!=ordinal) {cursor+=size;continue;}
							if (!quad_bounds(quad.data(), rect)) return false;
							result.weapon_blur=blur;result.weapon_border=rect;
							result.bounds = {std::floor(rect.x) - 4, std::floor(rect.y) - 4,
								std::ceil(rect.right) + 4, std::ceil(rect.bottom) + 4};
							found = true;
						}
						if (flags==0 && pass == 1 && name.starts_with("h1_hud_weapwidget_") && quad_bounds(quad.data(), rect))
							selected = rect.x >= result.bounds.x && rect.y >= result.bounds.y &&
								rect.right <= result.bounds.right && rect.bottom <= result.bounds.bottom;
					}
					if (pass == 1 && flags == 0 && op == 20 && size > native_hud_quad::text_payload_offset && size <= 1024)
					{
						std::array<float, 2> xy{};
						if (!utils::native_memory::read_bytes(xy.data(), cursor + 4, sizeof(xy))) return false;
						const auto x = xy[0], y = xy[1];
						selected = std::isfinite(x) && std::isfinite(y) && x >= result.bounds.x &&
							x <= result.bounds.right && y >= result.bounds.y && y <= result.bounds.bottom;
					}
					if (selected)
					{
						// Proven native script ownership outranks screen-space overlap
						// with either hand's ammo panel. Never carry results with a gun.
						if(native_waypoints::owner(narrative_groups,reinterpret_cast<std::uintptr_t>(cursor),size)!=native_waypoints::text_owner::none)
						{cursor+=size;continue;}
						if (result.count == result.selected.size()) return false;
						result.selected[result.count++] = cursor;
					}
					cursor += size;
				}
				if (!terminated || !found || (pass==0 && borders!=expected)) return false;
			}
			const auto width = result.bounds.right - result.bounds.x;
			const auto height = result.bounds.bottom - result.bounds.y;
			return result.count > 1 && result.bounds.x >= 0 && result.bounds.y >= 0 &&
				width >= 32 && width <= 2048 && height >= 16 && height <= 1024;
		}
