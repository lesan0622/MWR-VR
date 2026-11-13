#include "launcher/dxvk_install.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
static void write(const fs::path& p, const char* text) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << text; }
static bool throws(const fs::path& g, bool on) { try { launcher_dxvk::sync(g, on); return false; } catch (const std::exception& e) { std::cout << "  error: " << e.what() << "\n"; return true; } }
int main()
{
	const auto g = fs::temp_directory_path() / "mwrvr_dxvk_sync";
	fs::remove_all(g);
	write(g / "overlord-mwr/dxvk/d3d11.dll", "DXVK-D3D11");
	write(g / "overlord-mwr/dxvk/dxgi.dll", "DXVK-DXGI");
	launcher_dxvk::sync(g, false);                    // off, nothing installed
	assert(!fs::exists(g / "d3d11.dll") && !fs::exists(g / "dxgi.dll"));
	launcher_dxvk::sync(g, true);                     // install
	assert(fs::exists(g / "d3d11.dll") && fs::exists(g / "dxgi.dll"));
	launcher_dxvk::sync(g, true);                     // idempotent
	launcher_dxvk::sync(g, false);                    // remove ours
	assert(!fs::exists(g / "d3d11.dll") && !fs::exists(g / "dxgi.dll"));
	write(g / "dxgi.dll", "RESHADE");                 // foreign wrapper
	assert(throws(g, true));                          // refuse to overwrite
	assert(!fs::exists(g / "dxgi.dll.mwrvr-partial"));
	assert(!fs::exists(g / "d3d11.dll"));             // no half-installed pair
	launcher_dxvk::sync(g, false);                    // never delete foreign
	{ std::ifstream f(g / "dxgi.dll"); std::string s; f >> s; assert(s == "RESHADE"); }
	fs::remove(g / "dxgi.dll");
	fs::remove_all(g / "overlord-mwr");
	assert(throws(g, true));                          // bundle missing
	fs::remove_all(g);
	std::cout << "DXVK_SYNC_OK\n";
}
