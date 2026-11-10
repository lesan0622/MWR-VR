#include <std_include.hpp>
#include "game/assets.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"

int main()
{
	using Microsoft::WRL::ComPtr;
	ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
	if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
	unsigned failed{};
	const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<why<<'\n';}};
	std::array<game::GfxPackedVertex,6> vertices{};
	for(unsigned i=0;i<6;++i){vertices[i].xyz[0]=float(i);vertices[i].xyz[1]=float(i%3);}
	std::array<game::Face,2> faces{{{0,1,2},{3,4,5}}};
	std::array<unsigned short,6> weights{0,0,64,64,64,64};
	std::array<game::DObjAnimMat,2> bind{{{{0,0,0,1},{0,0,0},2},{{0,0,0,1},{0,0,0},2}}};
	std::array<game::scr_string_t,2> names{1,2};
	game::Material material{};std::array<game::Material*,2> materials{&material,&material};
	game::XRigidVertList group{0,6,0,2,nullptr};
	std::array<game::XSurface,2> surfaces{};
	for(auto& s:surfaces){s.flags=2;s.vertCount=6;s.triCount=2;s.triIndices=s.triIndices2=faces.data();s.verts0.packedVerts0=vertices.data();}
	surfaces[0].rigidVertListCount=1;surfaces[0].rigidVertLists=&group;
	surfaces[1].flags=6;surfaces[1].blendVertCounts[0]=6;surfaces[1].blendVerts=weights.data();surfaces[1].partBits[0]=int(0xc0000000u);
	D3D11_BUFFER_DESC vb{};vb.ByteWidth=sizeof(vertices);vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vdata{vertices.data(),0,0};ComPtr<ID3D11Buffer> vertex_buffer;
	check(SUCCEEDED(device->CreateBuffer(&vb,&vdata,&vertex_buffer)),"source vertex buffer failed");
	for(auto& s:surfaces)s.vb0=vertex_buffer.Get();
	game::XModel source{};source.numBones=2;source.numRootBones=1;source.numsurfs=2;source.numLods=1;
	source.baseMat=bind.data();source.boneNames=names.data();source.materialHandles=materials.data();source.lodInfo[0].surfs=surfaces.data();source.lodInfo[0].numsurfs=2;source.scale=1;
	const std::array<scene_models::surface_face_range,1> moving{{{0,1,1}}};
	const scheduler::detail::execution_scope main(scheduler::pipeline::main);
	for(unsigned mode=0;mode<3;++mode)
	{
		D3D11_BUFFER_DESC ib{};ib.ByteWidth=sizeof(faces);ib.Usage=D3D11_USAGE_IMMUTABLE;ib.BindFlags=D3D11_BIND_INDEX_BUFFER|(mode?D3D11_BIND_SHADER_RESOURCE:0);
		D3D11_SUBRESOURCE_DATA idata{faces.data(),0,0};ComPtr<ID3D11Buffer> index_buffer;
		check(SUCCEEDED(device->CreateBuffer(&ib,&idata,&index_buffer)),"source index buffer failed");
		ComPtr<ID3D11ShaderResourceView> view;
		if(mode){D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=mode==1?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT;desc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;desc.Buffer.NumElements=mode==1?6:3;
			check(SUCCEEDED(device->CreateShaderResourceView(index_buffer.Get(),&desc,&view)),"source index view failed");}
		for(auto& s:surfaces){s.indexBuffer=index_buffer.Get();s.indexBufferView=view.Get();}
		scene_models::rigid_part bolt,remainder,strict;
		check(bolt.create_rigid_face_partition(&source,0,moving,true),"moving rigid piece rejected");
		check(remainder.create_rigid_face_partition(&source,0,moving,false),"rigid remainder rejected");
		check(!strict.create_face_partition(&source,0,moving,false),"ordinary partition silently discarded deforming geometry");
		if(bolt.model() && remainder.model())
		{
			const auto& b=bolt.model()->lodInfo[0].surfs[0];const auto& r=remainder.model()->lodInfo[0].surfs[0];
			check(b.triCount==1 && r.triCount==1 && b.triIndices[0].v1==3 && r.triIndices[0].v1==0,"rigid faces did not split losslessly");
			check(bolt.model()->numsurfs==1 && remainder.model()->numsurfs==1,"native skin was frozen into the rigid copy");
		}
		check(surfaces[1].flags==6 && surfaces[1].blendVerts==weights.data() && weights[2]==64 && faces[1].v1==3,"native deforming asset changed");
	}
	std::cout<<"Rigid partition with native mixed skin WARP: "<<failed<<" failures\n";
	return failed?1:0;
}
