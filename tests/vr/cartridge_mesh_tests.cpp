#include <std_include.hpp>
#include "game/assets.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include "component/vr/gameplay/chamber_cartridge.hpp"

int main()
{
	using Microsoft::WRL::ComPtr;
	ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
	if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
	unsigned failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<why<<'\n';}};
	std::vector<game::GfxPackedVertex> vertices;std::vector<game::Face> faces;
	const auto box=[&](float first,float last,float radius,float height){
		const auto base=static_cast<unsigned short>(vertices.size());
		for(unsigned i=0;i<8;++i){game::GfxPackedVertex v{};v.xyz[0]=(i&1)?last:first;v.xyz[1]=(i&2)?radius:-radius;v.xyz[2]=height+((i&4)?radius:-radius);vertices.push_back(v);}
		constexpr unsigned short tris[][3]{{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},{2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};
		for(const auto& t:tris)faces.push_back({static_cast<unsigned short>(base+t[0]),static_cast<unsigned short>(base+t[1]),static_cast<unsigned short>(base+t[2])});
	};
	for(float height:{0.f,.4f}){box(0,.8f,.1f,height);box(.82f,1.25f,.08f,height);}
	game::XRigidVertList group{0,static_cast<unsigned short>(vertices.size()),0,static_cast<unsigned short>(faces.size()),nullptr};
	game::XSurface surface{};surface.flags=2;surface.vertCount=group.vertCount;surface.triCount=group.triCount;surface.rigidVertListCount=1;surface.rigidVertLists=&group;
	surface.verts0.packedVerts0=vertices.data();surface.triIndices=surface.triIndices2=faces.data();
	D3D11_BUFFER_DESC vb{};vb.ByteWidth=UINT(vertices.size()*sizeof(vertices[0]));vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vdata{vertices.data(),0,0};ComPtr<ID3D11Buffer> vertex_buffer;
	check(SUCCEEDED(device->CreateBuffer(&vb,&vdata,&vertex_buffer)),"source vertex buffer failed");surface.vb0=vertex_buffer.Get();
	D3D11_BUFFER_DESC ib{};ib.ByteWidth=UINT(faces.size()*sizeof(faces[0]));ib.Usage=D3D11_USAGE_IMMUTABLE;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA idata{faces.data(),0,0};ComPtr<ID3D11Buffer> index_buffer;
	check(SUCCEEDED(device->CreateBuffer(&ib,&idata,&index_buffer)),"source index buffer failed");surface.indexBuffer=index_buffer.Get();
	game::DObjAnimMat bind{{0,0,0,1},{0,0,0},2};game::scr_string_t name=1;
	game::Material material{};game::Material* material_pointer=&material;
	game::XModel source{};source.numBones=source.numRootBones=source.numsurfs=source.numLods=1;
	source.baseMat=&bind;source.boneNames=&name;source.materialHandles=&material_pointer;source.lodInfo[0].surfs=&surface;source.lodInfo[0].numsurfs=1;source.scale=1;
	const scheduler::detail::execution_scope main(scheduler::pipeline::main);
	scene_models::rigid_part round;
	check(round.create_rigid_bone(&source,0) && round.isolate_cartridge(),"one cartridge could not be separated from its own stack");
	if(const auto* model=round.model()){
		const auto& s=model->lodInfo[0].surfs[0];
		check(s.triCount==24,"cartridge must include one case and one projectile only");
		check(std::abs(model->bounds.midPoint[2]-.4f)<.0001f && std::abs(model->bounds.halfSize[0]-.625f)<.0001f,"one complete top cartridge was not retained");
		check(round.make_two_sided(),"receiver interior windings could not be prepared");
		const auto& doubled=round.model()->lodInfo[0].surfs[0];check(doubled.triCount==48,"interior windings did not preserve all original faces");
		for(unsigned i=0;i<24;++i)check(doubled.triIndices[i].v1==doubled.triIndices[i+24].v1 && doubled.triIndices[i].v2==doubled.triIndices[i+24].v3 && doubled.triIndices[i].v3==doubled.triIndices[i+24].v2,"opposite interior winding is wrong");
	}
	check(surface.triCount==48 && surface.triIndices==faces.data() && source.lodInfo[0].surfs==&surface,"native weapon geometry was mutated");
	namespace w=vr::gameplay::weapons;
	w::reload_profile p;p.interaction.full_stroke=.05f;p.interaction.slide_axis={-1,0,0};
	w::mechanics::state ammo;ammo.chamber_loaded=true;
	const vr::gameplay::hands::anchor rest{{2,0,4},{0,0,0,1}};
	check(bool(w::chamber_cartridge_pose(p,ammo,0,0,39.37007874f,&rest)),"loaded chamber disappeared with magazine absent");
	const auto pulled=w::chamber_cartridge_pose(p,ammo,.01f,0,39.37007874f,&rest);
	check(pulled && std::abs(pulled->position[0]-1.6062992f)<.0001f,"live round did not follow partial extraction");
	ammo.chamber_loaded=false;ammo.magazine_rounds=30;
	check(!w::chamber_cartridge_pose(p,ammo,0,0,39.37007874f,&rest),"magazine ammunition created a visual chamber round");
	std::cout<<"Own single cartridge and opaque interior WARP: "<<failed<<" failures\n";return failed?1:0;
}
