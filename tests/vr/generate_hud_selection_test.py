"""Compile the actual production selector against its pre-optimization oracle.

Generates a standalone C++ fixture; no game process or save files are needed.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/native_hud_capture.cpp').read_text(encoding='utf-8')
start = source.index('\t\tbool quad_bounds(')
end = source.index('\t\tvoid bind_weapon_blur', start)
selector = source[start:end]
baseline = (root / 'tests/vr/fixtures/weapon_hud_selector_baseline.inc').read_text(encoding='utf-8')
preamble = r'''
#include "component/vr/hud_command_snapshot.hpp"
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <random>
using namespace vr::native_hud_capture;
struct rectangle { float x{}, y{}, right{}, bottom{}; };
struct scope {
 std::array<const void*,128> selected{}; std::size_t count{};
 const void* weapon_blur{}; rectangle weapon_border{}, bounds{};
};
static std::vector<std::byte> stream;
static std::size_t reads{}, names{};
namespace utils::native_memory {
bool read_bytes(void* out, const void* in, std::size_t size) {
 ++reads; const auto at=reinterpret_cast<std::uintptr_t>(in);
 const auto begin=reinterpret_cast<std::uintptr_t>(stream.data());
 if(at<begin || at>begin+stream.size() || size>begin+stream.size()-at)return false;
 std::memcpy(out,in,size);return true;
}}
template<class T> T field(const void* in,std::size_t offset) {
 T value{};std::memcpy(&value,static_cast<const std::byte*>(in)+offset,sizeof(value));return value;
}
std::string_view material_name(const void* raw,std::array<char,96>&) {
 ++names;switch(field<unsigned>(raw,8)) {
 case 1:return "h1_hud_weapwidget_border";
 case 2:return "h1_hud_weapwidget_blur";
 case 3:return "h1_hud_weapwidget_ammo";
 default:return "unrelated";
 }}
namespace gameplay::weapon_hud::native {
bool border(std::string_view name){return name=="h1_hud_weapwidget_border";}
}
namespace native_hud_quad { constexpr unsigned text_payload_offset=60; }
namespace native_waypoints {
struct group { std::uintptr_t address{}; };
enum class text_owner { none, narrative };
text_owner owner(std::span<const group> groups,std::uintptr_t address,unsigned) {
 for(auto g:groups)if(g.address==address)return text_owner::narrative;
 return text_owner::none;
}}
'''
main = r'''
template<class T> void put(std::size_t at,T value){std::memcpy(stream.data()+at,&value,sizeof(value));}
void quad(unsigned material,float x,float y,unsigned flags=0,unsigned alpha=255) {
 const auto at=stream.size();stream.resize(at+104);
 put(at,std::uint16_t(104));put(at+2,std::uint8_t(17));put(at+3,std::uint8_t(flags));put(at+8,material);
 for(unsigned i=0;i<4;++i){put(at+16+i*16,x+(i%2)*64.f);put(at+20+i*16,y+(i/2)*32.f);}
 put(at+96,alpha<<24);
}
void text(float x,float y){const auto at=stream.size();stream.resize(at+64);put(at,std::uint16_t(64));put(at+2,std::uint8_t(20));put(at+4,x);put(at+8,y);}
int main(){
 command_snapshot snapshot;std::mt19937 random(42);std::size_t old_reads{},new_reads{},old_names{},new_names{},accepted{};
 for(unsigned trial=0;trial<1200;++trial){
  stream.clear();const unsigned sources=1+trial%2;
  for(unsigned i=0;i<sources;++i){quad(2,100.f+i*200,100,0x40);quad(1,100.f+i*200,100,0,trial%17?255:0);quad(3,100.f+i*200,100);text(120.f+i*200,120);}
  for(unsigned i=0;i<30;++i){if(random()%2)quad(3+random()%2,float(random()%600),float(random()%300));else text(float(random()%600),float(random()%300));}
  if(trial%13)stream.resize(stream.size()+4); // Missing terminators must reject.
  if(trial%19==0)put(0,std::uint16_t(2));
  if(trial%23==0)put(104+16,std::numeric_limits<float>::quiet_NaN());
  std::array<native_waypoints::group,1> groups{{{trial%3?0:reinterpret_cast<std::uintptr_t>(stream.data()+208)}}};
  snapshot.reset();
  for(unsigned i=0;i<sources;++i){
   scope before,after;reads=names=0;const bool a=baseline_select(stream.data(),before,i,sources,groups);old_reads+=reads;old_names+=names;
   reads=names=0;const bool b=select_commands(stream.data(),snapshot,after,i,sources,groups);new_reads+=reads;new_names+=names;
   assert(a==b);if(!a)continue;++accepted;
   assert(before.count==after.count && before.weapon_blur==after.weapon_blur);
   assert(std::memcmp(&before.bounds,&after.bounds,sizeof(rectangle))==0);
   assert(std::memcmp(&before.weapon_border,&after.weapon_border,sizeof(rectangle))==0);
   for(std::size_t n=0;n<before.count;++n)assert(before.selected[n]==after.selected[n]);
  }
 }
 assert(accepted>100);assert(new_reads<old_reads);assert(new_names<old_names);
 snapshot.reset();unsigned decoded{};
 for(unsigned i=0;i<2048;++i)assert(snapshot.get(i,[&](auto&){++decoded;return true;}));
 assert(!snapshot.get(2048,[&](auto&){++decoded;return true;}));assert(decoded==2048);
 snapshot.reset();assert(!snapshot.get(0,[](auto&){return false;}));assert(snapshot.get(0,[](auto&){return true;}));
 std::cout<<"1200 streams equivalent; accepted="<<accepted<<" reads="<<old_reads<<" -> "<<new_reads<<" materials="<<old_names<<" -> "<<new_names<<"\n";
}
'''
output = root / 'build/language-tests/hud_selection_test.cpp'
output.parent.mkdir(parents=True, exist_ok=True)
# quad_bounds is needed by both implementations; put it before the oracle.
split = selector.index('\t\tbool select_commands(')
output.write_text(preamble + selector[:split] + baseline + selector[split:] + main, encoding='utf-8')
print(output)
