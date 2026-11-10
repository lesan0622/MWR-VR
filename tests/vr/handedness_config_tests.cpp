#include <stdexcept>
#include "launcher/vr_settings_config.hpp"
#include <iostream>
#include <sstream>

int main()
{
	using namespace launcher_vr_settings;
	try
	{
		for(const char* controls:{"right","left"})for(const char* holster:{"right","left"})
		{
			auto values=defaults();values[vr::settings::control_hand.name]=controls;values[vr::settings::pistol_holster_side.name]=holster;
			if(!validate(values) || read_values(update_config("bind F10 togglemenu\n",values))!=values)
				throw std::runtime_error("Independent handedness selections failed validation/config round trip");
		}
		for(const auto& field:vr::settings::choices)
		{
			std::ostringstream key;key<<"0x"<<std::hex<<static_cast<std::uint32_t>(dvars::generate_hash(field.name));
			for(std::size_t i=0;i<field.values.size() && field.values[i];++i)
			{
				const auto saved="seta "+key.str()+" \""+std::to_string(i)+"\"\n";
				const auto loaded=read_values(saved);
				if(loaded[field.name]!=field.values[i] || update_config(saved,loaded).find(key.str())!=std::string::npos)
					throw std::runtime_error("Native saved enum did not restore/canonicalize preference");
			}
		}
		std::cout<<"Handedness config: four independent combinations and native enum aliases passed\n";
	}
	catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
