#pragma once

#include "structs.hpp"
#include "launcher/launcher.hpp"

namespace game
{
	namespace environment
	{
		launcher::mode get_mode();
		launcher::mode get_real_mode();

		bool is_sp();
		bool is_mp();
		bool is_dedi();

		void set_mode(launcher::mode mode);

		std::string get_string();
	}

	template <typename T>
	class symbol
	{
	public:
		symbol(const size_t address)
			: address_(reinterpret_cast<T*>(address))
		{
		}

		T* get() const
		{
			return reinterpret_cast<T*>(address_);
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}

	private:
		T* address_;
	};
}

#include "symbols.hpp"

namespace game
{
	inline void* R_AddCmdDrawText(const char* text, int max_chars, Font_s* font, float x, float y, float x_scale,
		float y_scale, float rotation, float* color, int style)
	{
		return H1_AddBaseDrawTextCmd(text, max_chars, font, R_GetFontHeight(font), x, y, x_scale, y_scale, rotation,
			color, style, -1, 0, R_GetTextStyle(style));
	}
}
