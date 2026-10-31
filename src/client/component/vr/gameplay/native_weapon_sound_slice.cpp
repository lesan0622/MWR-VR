#include <std_include.hpp>
#include "native_weapon_sound_slice.hpp"
#include "weapon_sound_cues.hpp"
#include "weapon_sound_slice.hpp"
#include "weapon_sound_window.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::weapons::native_weapon_sound_slice
{
	namespace
	{
		bool ready{};
		utils::hook::detour queue_hook, create_hook;
		// SND has 96 loaded channels and a separate 208-voice decoder pool.
		std::array<std::atomic<unsigned>,96> pending_ms{};
		std::array<std::atomic<std::uint64_t>,208> budgets{};
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
		thread_local const game::snd_alias_t* requested_alias{};
		thread_local unsigned requested_ms{}, creating_ms{};
		// MWR port tool (vr_sound_dump): decoded PCM of one loaded voice, written
		// as WAV so slice cues can be measured from the MWR recordings.
		std::array<std::atomic<bool>,96> pending_dump{};
		thread_local bool requested_dump{}, creating_dump{};
		struct pcm_dump
		{
			std::atomic<int> index{-1};
			std::vector<std::int16_t> pcm;
			unsigned channels{},rate{},expected{};
			std::string file;
		} dump;
		template<class T> T read(const void* p, size_t offset)
		{ T value; std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value; }
		std::atomic<std::uint64_t>* budget(void* source)
		{
			const auto address=reinterpret_cast<std::uintptr_t>(source);
			constexpr std::uintptr_t first=0x14C5D4680+0x28; // MWR voice pool (H2 0x14BC7E580, stride 0x2d8)
			if(address<first || (address-first)%0x290)return nullptr;
			const auto index=(address-first)/0x290;
			return index<budgets.size() ? &budgets[index] : nullptr;
		}
		int queue(void* info,int channel,int spatial,int flags)
		{
			// Reset on EVERY loaded-channel assignment, including ordinary sounds
			// and failed/replaced requests. Never inherit an earlier VR cutoff.
			if(channel>=0 && channel<int(pending_ms.size()))
			{
				const bool match=read<const game::snd_alias_t*>(info,0)==requested_alias;
				pending_ms[channel].store(match ? requested_ms : 0);
				pending_dump[channel].store(match && requested_dump);
			}
			return queue_hook.invoke<int>(info,channel,spatial,flags);
		}
		int create(void* info,int channel,int offset_ms)
		{
			const auto previous=creating_ms;const auto previous_dump=creating_dump;
			const bool valid=channel>=0 && channel<int(pending_ms.size());
			creating_ms=valid ? pending_ms[channel].exchange(0) : 0;
			creating_dump=valid && pending_dump[channel].exchange(false);
			const auto result=create_hook.invoke<int>(info,channel,offset_ms);
			creating_ms=previous;creating_dump=previous_dump;
			return result;
		}
		void* decoder(void* source,const void* format)
		{
			// This call site belongs only to loaded voices, before state=2 publishes
			// the voice to the audio worker. Reuse clears the previous voice budget.
			if(auto* slice=budget(source))
			{
				slice->store(creating_ms ? (std::uint64_t(1)<<32) | (std::uint64_t(creating_ms)*read<unsigned>(source,8)/1000) : 0);
				const auto index=int(slice-budgets.data());
				if(creating_dump)
				{
					dump.pcm.clear();dump.channels=read<unsigned>(source,4);dump.rate=read<unsigned>(source,8);
					dump.index.store(index);
				}
				else if(dump.index.load()==index)dump.index.store(-1); // Voice slot reused.
			}
			return utils::hook::invoke<void*>(0x1403D64C0,source,format); // MWR (H2 0x1405D5430)
		}
		struct pcm_ring {void* samples;unsigned write,count,starved,unknown;};
		static_assert(offsetof(pcm_ring,count)==0xc);
		void capture(const pcm_ring* ring,unsigned channels,unsigned before,bool ended)
		{
			// Frames produced by this fill end at ring->write (ring of 1920 int16).
			const unsigned capacity=1920/channels;
			if(ring->count>=before && ring->count<=capacity && ring->samples)
			{
				const unsigned produced=ring->count-before;
				const auto* samples=static_cast<const std::int16_t*>(ring->samples);
				for(unsigned f=0;f<produced;++f)
				{
					const unsigned frame=(ring->write+capacity-produced+f)%capacity;
					for(unsigned c=0;c<channels;++c)dump.pcm.push_back(samples[frame*channels+c]);
				}
			}
			const auto frames=unsigned(dump.pcm.size()/channels);
			if(!ended && frames<dump.expected)return;
			dump.index.store(-1);
			std::string wav(44,'\0');
			const auto put=[&](size_t at,std::uint32_t v,size_t n){std::memcpy(wav.data()+at,&v,n);};
			const std::uint32_t bytes=std::uint32_t(dump.pcm.size()*2);
			std::memcpy(wav.data(),"RIFF",4);put(4,36+bytes,4);std::memcpy(wav.data()+8,"WAVEfmt ",8);
			put(16,16,4);put(20,1,2);put(22,channels,2);put(24,dump.rate,4);put(28,dump.rate*channels*2,4);
			put(32,channels*2,2);put(34,16,2);std::memcpy(wav.data()+36,"data",4);put(40,bytes,4);
			wav.append(reinterpret_cast<const char*>(dump.pcm.data()),bytes);
			const auto path="overlord-mwr/captures/_sounds/"+dump.file+".wav";
			utils::io::write_file(path,wav);
			console::info("[VR audio] dumped %s: %u frames (expected %u) %u Hz x%u -> %s\n",
				dump.file.c_str(),frames,dump.expected,dump.rate,channels,path.c_str());
		}
		bool fill(void* decoder_state,void* source,pcm_ring* ring)
		{
			auto* stored=budget(source);
			const auto packed=stored ? stored->load() : 0;
			sound_slice_budget slice{(packed>>32)!=0,static_cast<unsigned>(packed)};
			// Streamed/primed sources have their own lifetime and seek path. They
			// never use the loaded-voice budget, including recycled voice slots.
			const auto channels=read<unsigned>(source,4);
			// Native ring capacity is 1920 interleaved int16 samples. The engine
			// frees and NULLS the decoder on true, then keeps calling fill while
			// retained PCM drains. Retire our budget with that single release edge.
			const bool dumping=stored && dump.index.load()==int(stored-budgets.data()) && decoder_state && channels;
			const auto before=ring->count;
			const bool ended=slice.fill(decoder_state!=nullptr,read<void*>(source,0x20)!=nullptr,
				channels ? 1920/channels : 0,ring->write,ring->count,
				[&]{return utils::hook::invoke<bool>(0x1403D6880,decoder_state,source,ring);}); // MWR (H2 0x1405D57F0)
			if(stored)stored->store(slice.enabled ? (std::uint64_t(1)<<32)|slice.remaining : 0);
			if(dumping)capture(ring,channels,before,ended);
			return ended;
		}
		template<size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
		bool copy_loaded(const game::snd_alias_t* alias,std::array<char,256>& name,unsigned& rate,unsigned& frames) noexcept
		{
			__try
			{
				if(!alias || (alias->flags&0x21) || !alias->soundFile || alias->soundFile->type!=game::SAT_LOADED || !alias->soundFile->exists)return false;
				const auto* loaded=alias->soundFile->u.loadSnd;if(!loaded || !loaded->name)return false;
				rate=read<unsigned short>(loaded,0x28);frames=read<unsigned>(loaded,0x30);
				for(size_t i=0;i<name.size();++i){name[i]=loaded->name[i];if(!name[i])return rate && frames && i;}
				name.back()=0;
				return false;
			}
			__except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
		}
	}
	bool initialize()
	{
		if(ready)return true;
		constexpr std::uint8_t queue_entry[]{0x48,0x89,0x5c,0x24,0x08};
		constexpr std::uint8_t create_entry[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
		// MWR sites (H2 + ~0x1FEF00); rel32 operands re-read from h1_sp64.
		constexpr std::uint8_t decoder_call[]{0xe8,0x82,0x75,0xff,0xff};
		constexpr std::uint8_t fill_call[]{0xe8,0xb0,0x75,0xff,0xff};
		constexpr std::uint8_t seek[]{0x41,0x8b,0x41,0x44,0x85,0xc0};
		constexpr std::uint8_t ring_capacity[]{0xb8,0x80,0x07,0x00,0x00};
		constexpr std::uint8_t null_decoder[]{0x4d,0x85,0xf6,0x0f,0x84,0x33,0x02,0x00,0x00};
		constexpr std::uint8_t release_then_null[]{0x84,0xc0,0x74,0x0d,0x48,0x8b,0x4b,0x38,0xe8,0x53,0x74,0xff,0xff,0x4c,0x89,0x73,0x38};
		if(!verify(0x1403D1920,queue_entry) || !verify(0x1403D5FD0,create_entry) ||
			!verify(0x1403DEF39,decoder_call) || !verify(0x1403DF2CB,fill_call) || !verify(0x1403D033B,seek) ||
			!verify(0x1403D68FD,ring_capacity) || !verify(0x1403D68CC,null_decoder) ||
			!verify(0x1403DF2D0,release_then_null))return false;
		queue_hook.create(0x1403D1920,queue);create_hook.create(0x1403D5FD0,create);
		utils::hook::call(0x1403DEF39,decoder);utils::hook::call(0x1403DF2CB,fill);
		ready=true;return true;
	}
	bool play(short entity,const float* origin,const char* name,sound_part part,const sound_window* window)
	{
		if(part==sound_part::whole && !window)return utils::hook::invoke<int>(0x14015C4A0,entity,origin,name)!=-1; // MWR play (H2 0x140380430)
		// MWR: recordings without an authored cue (or no slice contract) play the
		// complete alias once on the first part; the second part stays silent.
		const auto whole_once=[&]{return part!=sound_part::second && utils::hook::invoke<int>(0x14015C4A0,entity,origin,name)!=-1;};
		if(!ready)return whole_once();
		// Keep the stock alias selection/randomization, WeaponDef mapping, spatial
		// bus and bookkeeping. No shared alias or extracted sound is modified.
		const auto* alias=utils::hook::invoke<const game::snd_alias_t*>(0x1401A3220,name,int(entity)); // MWR (H2 0x1403CA0D0)
		std::array<char,256> file{};unsigned rate{},frames{};
		const auto* cue=copy_loaded(alias,file,rate,frames) ? find_weapon_sound_cue(file.data(),rate,frames) : nullptr;
		const auto range=cue?resolve_sound_window(*cue,part,window):resolved_sound_window{};
		if(!range.valid)
		{
			static std::string last;
			if(last!=file.data()){last=file.data();console::warn("[VR audio] No slice cue: alias=%s file=%s rate=%u frames=%u\n",name,file.data(),rate,frames);}
			return whole_once();
		}
		const auto start=range.begin_ms;
		const auto* previous_alias=requested_alias;const auto previous_ms=requested_ms;
		requested_alias=alias;requested_ms=range.duration_ms;
		const auto id=utils::hook::invoke<int>(0x1405D82D0,alias,int(entity),origin,int(start),1,0,nullptr); // MWR (H2 0x1407C7090)
		requested_alias=previous_alias;requested_ms=previous_ms;
		utils::hook::invoke<void>(0x1405D3080,id,alias,1,0); // MWR (H2 0x1407C1CC0)
		return id!=-1;
	}
	std::string dump_alias(short entity,const float* origin,const char* name)
	{
		if(!ready)return "slice contract not ready";
		if(dump.index.load()>=0)return "dump already running";
		const auto* alias=utils::hook::invoke<const game::snd_alias_t*>(0x1401A3220,name,int(entity));
		std::array<char,256> file{};unsigned rate{},frames{};
		if(!copy_loaded(alias,file,rate,frames))return "alias not a loaded sound";
		std::string_view base(file.data());
		if(const auto slash=base.find_last_of("/\\");slash!=base.npos)base.remove_prefix(slash+1);
		dump.file=std::string(base);dump.expected=frames;
		requested_alias=alias;requested_ms=0;requested_dump=true;
		const auto id=utils::hook::invoke<int>(0x1405D82D0,alias,int(entity),origin,0,1,0,nullptr);
		requested_alias=nullptr;requested_dump=false;
		utils::hook::invoke<void>(0x1405D3080,id,alias,1,0);
		return std::format("{} file={} rate={} frames={} id={}",name,dump.file,rate,frames,id);
	}
}
