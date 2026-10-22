#pragma once
#include <array>
#include <string_view>

namespace vr::gameplay::weapons
{
 struct weapon_sound_cue { std::string_view file; unsigned rate,frames,split_ms; };
 // Waveform-guided test cues, in source milliseconds (not animation time).
 // Match the selected recording and its format, never the alias name alone.
 inline constexpr weapon_sound_cue weapon_sound_cues[]{
  {"wpfoly_ak47_reload_chamber_v4",48000,44307,175},
  {"wpfoly_beretta9mm_reload_clipout_v2",48000,65321,495},
  {"wpfoly_de50_reload_chamber_v1",48000,27852,163},
  {"wpfoly_de50_reload_chamber_v1_short",48000,27300,151},
  {"wpfoly_m14_reload_chamber_v1",48000,36575,182},
  {"wpfoly_m40a3_rechamber_v1",48000,54206,372},
  {"wpfoly_m4_reload_chamber",48000,25474,137},
  {"wpfoly_m82_reload_chamber_v1",48000,41847,209},
  {"wpfoly_m82_reload_clipout_v1",48000,79344,345},
  {"wpfoly_miniuzi_reload_chamber_v1",48000,29561,124},
  {"wpfoly_p90_reload_chamber_v1",48000,30880,105},
  {"wpfoly_rpd_reload_chamber_v1",48000,55032,204},
  {"wpfoly_rpd_reload_chamber_v1_quick",48000,55032,144},
  {"wpfoly_rpg_reload_lift_v1",48000,40969,167},
  {"wpfoly_usp_reload_chamber_v1",48000,25087,199},
  {"wpfoly_usp_reload_clipout_v2",48000,67733,527},
  {"wpn_h2_aa12_foley_chamber_01",48000,26364,275},
  {"wpn_h2_aug_foley_chamber_01",48000,30863,255},
  {"wpn_h2_aug_foley_clipout_01",48000,77285,424},
  {"wpn_h2_fal_foley_chamber_01",48000,29020,202},
  {"wpn_h2_famas_foley_chamber_01",48000,59588,176},
  {"wpn_h2_famas_foley_clipout_01",48000,72006,392},
  {"wpn_h2_fn2000_foley_chamber_01",48000,32464,150},
  {"wpn_h2_fn2000_foley_clipout_01",48000,74330,211},
  {"wpn_h2_g18_foley_chamber_pullout_01",48000,18745,97},
  {"wpn_h2_kriss_foley_chamber_01",48000,56086,161},
  {"wpn_h2_kriss_foley_clipout_01",48000,54429,590},
  {"wpn_h2_masada_foley_chamber_01",48000,54188,142},
  {"wpn_h2_mg4_foley_chamber_01",48000,98029,269},
  {"wpn_h2_sa80_foley_chamber_01",48000,37676,219},
  {"wpn_h2_scar_h_foley_chamber_01",48000,27730,205},
  {"wpn_h2_tavor_foley_chamber_01",48000,43876,108},
  {"wpn_h2_tmp_foley_chamber_01",48000,34180,149},
  {"wpn_h2_ump45_foley_chamber_01",48000,44026,173},
  {"wpn_h2_wa2000_foley_chamber_01",48000,28950,225},
  {"wpfoly_ak47_reload_chamber_v4",44100,32002,160},
  {"wpfoly_mp9_reload_chamber_v1",44100,40878,195},
  {"wpfoly_ump45_reload_chamber_v1",44100,34523,180},
  // MWR recordings: silent-gap splits measured by port/sound_split.py from
  // decoded voices (vr_sound_dump); 10 ms before the second event's onset.
  {"wpfoly_ak47_reload_clipout_v5",48000,50436,167},
  {"wpfoly_ak74u_reload_chamber_v4",48000,34322,224},
  {"wpfoly_beretta9mm_reload_clipout_v2",48000,44696,451},
  {"wpfoly_colt1911_reload_clipout_v1",48000,36724,168},
  {"wpfoly_de50_reload_chamber_v1",48000,24752,104},
  {"wpfoly_dragunov_reload_chamber_v1",48000,35806,264},
  {"wpfoly_dragunov_reload_clipout_v2",48000,56260,138},
  {"wpfoly_g36_reload_chamber_v1",48000,37629,78},
  {"wpfoly_g36_reload_clipout_v1",48000,39317,85},
  {"wpfoly_g3_reload_chamber_v1",48000,28927,151},
  {"wpfoly_g3_reload_clipout_v2",48000,56709,140},
  {"wpfoly_m14_reload_chamber_v1",48000,35938,211},
  {"wpfoly_m14_reload_clipout_v1",48000,43991,196},
  {"wpfoly_m249_reload_chamber_v1",48000,73066,534},
  {"wpfoly_m249_reload_clipout_v1",48000,74247,137},
  {"wpfoly_m40a3_rechamber_v1",48000,54206,634},
  {"wpfoly_m4_reload_chamber",48000,24333,130},
  {"wpfoly_m4_reload_clipout",48000,29222,142},
  {"wpfoly_miniuzi_reload_chamber_v1",48000,38056,166},
  {"wpfoly_mp5_reload_chamber_v3",48000,46604,116},
  {"wpfoly_mp5_reload_clipout_v3",48000,67227,109},
  {"wpfoly_p90_reload_chamber_v1",48000,27884,163},
  {"wpfoly_p90_reload_clipout_v1",48000,41304,18},
  {"wpfoly_rpd_reload_chamber_v1",48000,95790,246},
  {"wpfoly_skorpion_reload_chamber_v1",48000,29125,38},
  {"wpfoly_winchester_reload_pump_v1",48000,27288,220},
 };
 inline const weapon_sound_cue* find_weapon_sound_cue(std::string_view file,unsigned rate,unsigned frames) noexcept
 {
  file.remove_prefix(file.find_last_of("/\\")==std::string_view::npos ? 0 : file.find_last_of("/\\")+1);
  if(file.ends_with(".flac") || file.ends_with(".wav"))file=file.substr(0,file.find_last_of('.'));
  for(const auto& cue:weapon_sound_cues)
   if(file==cue.file && rate==cue.rate && frames==cue.frames)return &cue;
  return nullptr;
 }
}
