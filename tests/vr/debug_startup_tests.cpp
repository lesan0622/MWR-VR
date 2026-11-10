#include <stdexcept>
#include "launcher/vr_settings_config.hpp"
#include <iostream>
#include <sstream>

int main()
{
	using namespace launcher_vr_settings;
	try
	{
		for(std::size_t i=0;i<vr::debug_options::names.size();++i)
		{
			const auto* name=vr::debug_options::names[i];
			std::ostringstream hash;hash<<"0x"<<std::hex<<static_cast<std::uint32_t>(dvars::generate_hash(name));
			const auto config="seta "+hash.str()+" \"1\"\n";
			if(!debug_selection(read_values(config))[i])throw std::runtime_error("Hashed debug setting ignored: "+std::string(name));
			auto wanted=defaults();wanted[name]=true;
			const auto merged=update_config("seta "+hash.str()+" \"0\"\nseta cg_fov 90\n",wanted);
			if(merged.find(hash.str())!=std::string::npos || merged.find("seta cg_fov 90")==std::string::npos ||
				!debug_selection(read_values(merged))[i])throw std::runtime_error("Debug alias merge failed");
			const auto malformed=read_values("seta "+hash.str()+" \"true\"\n");
			if(debug_selection(malformed)[i])throw std::runtime_error("Malformed debug setting enabled probe");
		}
		auto values=defaults();for(const auto* name:vr::debug_options::names)values[name]=true;
		const auto selected=debug_selection(read_values(update_config("",values)));
		vr::debug_options::initialize(selected);
		vr::debug_options::initialize({}); // A later registration/default pass cannot disable active probes.
		for(std::size_t i=0;i<selected.size();++i)
			if(!vr::debug_options::enabled(static_cast<vr::debug_options::probe>(i)))throw std::runtime_error("Startup debug state lost");
		std::cout<<"11 debug switches: hashed config, merge, malformed input and startup freeze passed\n";
	}
	catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
