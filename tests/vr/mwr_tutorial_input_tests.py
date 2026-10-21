"""Run the MWR controller's production tutorial blocks with native endpoints replaced."""
from pathlib import Path
import json
import os
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/gameplay/controller_component.cpp').read_text(encoding='utf-8')
attack_start = source.index('\t\t\tconst auto held = gameplay::weapons::carry::held_instances();')
attack = source[attack_start:source.index('\t\t\tif (gameplay && input.secondary', attack_start)]
ads_start = source.index('\t\t\tif (local_client==0 && result)')
ads = source[ads_start:source.index('\t\t\tif (local_client == 0 && result)', ads_start)]
out = root / 'build/mwr-tutorial-input-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include "component/vr/gameplay/controller_ads.hpp"
#include "component/vr/digital_button_gate.hpp"
#include <cassert>
#include <iostream>
#include <tuple>
#include <vector>
using namespace vr;
using namespace std::chrono;
namespace fixture {
controller_input::frame input;
gameplay::weapons::hold owner;
gameplay::weapons::muzzle_frame muzzle;
struct held_weapon {gameplay::weapons::hold owner;};
std::array<held_weapon,2> held;
bool allowed=true;
std::vector<int> bindings;
}
namespace game {
constexpr int BUTTON_ADS=0x800, PMF_MANTLE=1, PMF_LADDER=2;
struct command {unsigned weapon=7; int buttons=0;} cmd;
struct player {int pm_flags=0;} ps;
const player* CG_GetPredictedPlayerState(int client) {assert(client==0); return &ps;}
}
namespace vr::controller_input { frame latest() noexcept {return fixture::input;} }
namespace vr::gameplay::scripted_control {bool predicted_allowed() {return fixture::allowed;} }
namespace vr::gameplay::weapons {
hold sample_native_equipped() noexcept {return fixture::owner;}
muzzle_frame current_muzzle() noexcept {return fixture::muzzle;}
bool firing_enabled() noexcept {return true;}
fire_delivery controller_fire_delivery(std::uint32_t,bool) noexcept {return fire_delivery::bullets;}
namespace carry {std::array<fixture::held_weapon,2> held_instances() {return fixture::held;} }
}
namespace utils::hook {
template<class Result,class... Args> Result invoke(std::uintptr_t address, Args... args) {
    assert(address==0x1401ACBD0);
    auto values=std::make_tuple(args...);
    assert(std::get<0>(values)==0 && std::get<2>(values)==0);
    fixture::bindings.push_back(std::get<1>(values));
}
}
namespace vr::controllers {
constexpr std::uintptr_t notify_command_address=0x1401ACBD0;
int building_client=0;
bool binding_verified=true, ads_verified=true, normal_input_seen=true;
std::atomic_bool ads_requested{};
unsigned attack_notify_count{},ads_notify_count{},ads_command_count{};
struct option {struct {bool enabled=true;} current;} ads_option;
option* auto_ads=&ads_option;
gameplay::weapons::ads_policy ads;
std::array<controller_input::digital_press_gate,2> attack_notifications{};
std::array<gameplay::weapons::hold,2> notified_owners{};
bool gameplay_allowed(const controller_input::frame&) {return fixture::allowed;}
void attack_step(controller_input::clock::time_point now,bool weapon_gameplay=true) {
    const auto input=controller_input::latest_interaction();
'''+attack+r'''
}
void ads_step(controller_input::clock::time_point now) {
    const int local_client=0;
    auto* result=&game::cmd;
'''+ads+r'''
}
}
int main() {
    using namespace vr::controllers;
    using namespace fixture;
    using namespace vr::gameplay::weapons;
    auto now=controller_input::clock::now();
    const auto prepare=[&] {
        bindings.clear(); input={}; held={}; attack_notifications={}; notified_owners={};
        ads.reset(); ads_requested=false; game::cmd={}; game::ps={}; allowed=true;
        input.sequence=input.reference_generation=input.continuity_generation=1;
        input.focused=true; input.sampled_at=now;
        for(auto& grip:input.grip) {grip.valid=true;grip.quality={true,true,true,true,true};}
        for(auto& trigger:input.trigger) {trigger.active=true;trigger.generation=1;}
        owner={7,1,hand::right,hand::left,hold_source::interaction,1}; owner.instance_generation=1;
        held[1].owner=owner;
        muzzle={}; muzzle.valid=true; muzzle.owner=owner; muzzle.units_per_meter=40;
        muzzle.input_sequence=muzzle.reference_generation=1;
        muzzle.position={24,0,-2.4f}; muzzle.head_forward={1,0,0};
        muzzle.axis={{{1,0,0},{0,1,0},{0,0,1}}};
        muzzle.sampled_at=muzzle.camera_at=now;
    };
    const auto advance=[&](int ms) {
        now+=milliseconds(ms); ++input.sequence; input.sampled_at=now;
        muzzle.sampled_at=muzzle.camera_at=now; muzzle.input_sequence=input.sequence;
    };
    for(unsigned h:{0u,1u}) {
        prepare(); held={}; owner.rear=static_cast<hand>(h); owner.support=hand::none; held[h].owner=owner;
        attack_step(now); assert(bindings.empty());
        input.trigger[h].down=true; ++input.trigger[h].presses;
        attack_step(now); attack_step(now);
        assert((bindings==std::vector<int>{1})); // One reliable +attack for either firing hand.
        input.trigger[h].down=false; attack_step(now);
        input.trigger[1-h].down=true; ++input.trigger[1-h].presses; attack_step(now);
        assert(bindings.size()==1); // An empty/support hand cannot notify a shot.
        input.trigger[h].down=true; ++input.trigger[h].presses; attack_step(now,false);
        attack_step(now); assert(bindings.size()==1); // No replay on gameplay recovery.
        input.trigger[h].down=false; attack_step(now);
        input.trigger[h].down=true; ++input.trigger[h].presses; attack_step(now);
        assert((bindings==std::vector<int>{1,1}));
    }
    prepare(); attack_step(now);
    input.trigger[1].down=true; ++input.trigger[1].presses;
    attack_step(now+milliseconds(151)); assert(bindings.empty());
    advance(152); attack_step(now); assert(bindings.empty()); // Stale press requires release.

    prepare(); ads_step(now); assert(!ads_requested && game::cmd.buttons==0);
    advance(50); ads_step(now); assert(bindings.empty());
    advance(50); ads_step(now);
    assert(ads_requested && (game::cmd.buttons&game::BUTTON_ADS));
    assert((bindings==std::vector<int>{17})); // Raised rifle sends +speed_throw.
    game::cmd.buttons=0x20; advance(20); ads_step(now);
    assert(game::cmd.buttons==(0x20|game::BUTTON_ADS) && bindings.size()==1);
    muzzle.position[2]=-16; advance(20); ads_step(now);
    game::cmd.buttons=0x20; advance(100); ads_step(now);
    assert(!ads_requested && game::cmd.buttons==0x20);
    assert((bindings==std::vector<int>{17,18})); // Lowering sends -speed_throw once.
    game::cmd.buttons=game::BUTTON_ADS; advance(20); ads_step(now);
    assert(game::cmd.buttons==game::BUTTON_ADS); // Keyboard/native ADS is preserved.

    prepare(); game::cmd.weapon=8; ads_step(now); advance(120); ads_step(now);
    assert(!ads_requested && bindings.empty()); // Never project another weapon's ADS.
    prepare(); owner.support=hand::none; muzzle.owner=owner;
    ads_step(now); advance(120); ads_step(now); assert(bindings.empty());
    std::cout << "MWR production tutorial input: PASS (both hands, edges, stale input, ADS transitions, native buttons)\n";
}
'''
(out / 'adapter.cpp').write_text(harness, encoding='utf-8')
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installs = json.loads(subprocess.check_output([str(vswhere), '-all', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json', '-utf8'], encoding='utf-8'))
install = Path(max(installs, key=lambda x: tuple(map(int, x['installationVersion'].split('.'))))['installationPath'])
driver = out / 'test.cmd'
driver.write_text(f'@echo off\ncall "{install / "VC/Auxiliary/Build/vcvars64.bat"}" >nul\n'
    'if errorlevel 1 exit /b 1\n'
    f'cl /nologo /std:c++latest /EHsc /W4 /I"{root / "src/client"}" '
    'adapter.cpp /Fe:adapter.exe\nif errorlevel 1 exit /b 1\nadapter.exe\n', encoding='utf-8')
subprocess.run(['cmd.exe', '/d', '/c', str(driver)], cwd=out, check=True)
