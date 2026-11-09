# GPU work reduction without changing image quality

## Confirmed redundant work

`spatial_panel::renderer::draw_screen_layer` previously copied the whole eye
target and rasterized a full-screen quad for every narrative/screen HUD layer.
The unchanged `screen_ps` shader discards pixels outside its projected canvas
when the exterior fade is exactly zero. Those pixels needed neither the copy
nor the pixel shader invocation.

The renderer now bounds those two operations to the canvas. The copy retains
the same texture dimensions, format, and pixel coordinates. Its source region
includes a conservative halo for linear filtering and the existing blur kernel
(radius at most eight pixels). The original shader, sample kernel, color space,
blend operations, resolution, native rendering, scopes, and eye isolation remain
unchanged. Full-screen fades retain full-screen work. Extreme/nonfinite canvas
coordinates also fall back to full-screen bounds rather than relying on unsafe
floating-point rectangle inference. An entirely offscreen zero-fade layer queues
no draw. Immediate-context state is still restored through ExecuteCommandList.

The background texture remains full size; this is GPU copy bandwidth and shader
work reduction, not a claim of lower VRAM allocation or a resolution reduction.
Masked/panel blur mip generation and the native stereo conversion are untouched.

The region obeys the source/destination texel bounds required by Microsoft's
[CopySubresourceRegion contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion).

## Validation

`tests/vr/generate_screen_region_reference.py` generates an isolated copy of the
pre-change renderer from commit `c9c3f18`, under a separate namespace. The test
links that renderer and the current production renderer into the same process.
It does not launch or modify the installed game.

`tests/vr/screen_region_gpu_tests.cpp` compares 360 rendered outputs per device:
R16G16B16A16_FLOAT and R8G8B8A8_UNORM targets; random background/ink; fractional,
clipped, fullscreen and offscreen canvases; blur regions; subtractive ink;
full-screen fades; resize/reuse; and three sequential layers per case.

Both software D3D WARP and the available hardware D3D device passed with
**byte-identical readback** for all 360 outputs each. The test also checks extreme
projection fallback and preservation of the caller's scissor rectangle.

An isolated 1024x1024 draw with a centered half-width/half-height canvas measured
pixel shader invocations using D3D11 pipeline statistics:

| Device | Original | Optimized |
| --- | ---: | ---: |
| WARP | 1,050,624 | 267,284 |
| Hardware | 1,048,576 | 264,196 |

This is actual shader invocation count, as defined by
[PSInvocations](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_pipeline_statistics).
The suite's calculated copy volume was 136,513,080 -> 51,217,112 bytes (~62.5%
less), derived from the copy rectangles, not a hardware memory-bandwidth counter.
All GPU queries/readback are confined to the test, never the shipping frame loop.

To reproduce in an x64 Visual Studio developer prompt at the repository root:

```bat
python tests\vr\generate_screen_region_reference.py
cl /nologo /EHsc /std:c++20 /O2 /I build\gpu-region-tests /I src\client\component\vr tests\vr\screen_region_gpu_tests.cpp src\client\component\vr\spatial_panel_renderer.cpp build\gpu-region-tests\reference_panel.cpp /Fo:build\gpu-region-tests\ /Fe:build\gpu-region-tests\screen_region.exe /link d3d11.lib d3dcompiler.lib dxgi.lib
build\gpu-region-tests\screen_region.exe
build\gpu-region-tests\screen_region.exe hardware
```

## Limits

These results establish equivalent output on the tested inputs and reduced
work in this particular HUD path, not a campaign GPU bottleneck ranking or FPS
improvement. Fullscreen canvases/fades get little or no benefit. The previous
~20.6 ms compositor GPU sample covers far more than this path. Same-scene VR
frame-time measurement and headset checks remain necessary; stable 120 FPS at
maximum settings is not established. No game installation, saves, or release
assets were changed by this work.
