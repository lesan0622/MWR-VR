"""Prepare a frozen pre-optimization renderer for pixel-for-pixel D3D comparison."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'build/gpu-region-tests'
out.mkdir(parents=True, exist_ok=True)
base = 'c9c3f18d823ee6e053f8b512df758681db5ca1bf'
for name in ('spatial_panel_renderer.hpp', 'spatial_panel_renderer.cpp'):
    data = subprocess.check_output(['git', '-C', str(root), 'show',
        f'{base}:src/client/component/vr/{name}']).decode()
    data = data.replace('namespace vr::spatial_panel',
        'namespace vr::spatial_panel_reference').replace('"spatial_panel_renderer.hpp"', '"reference_panel.hpp"')
    if name.endswith('.hpp'):
        data = '#include "spatial_panel_renderer.hpp"\n' + data.replace(
            'namespace vr::spatial_panel_reference\n{',
            'namespace vr::spatial_panel_reference\n{\nusing namespace vr::spatial_panel;')
    (out / ('reference_panel.hpp' if name.endswith('.hpp') else 'reference_panel.cpp')).write_text(data, encoding='utf-8')
# These isolated renderer translation units need no game PCH or native hooks.
(out / 'std_include.hpp').write_text('''#pragma once
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <climits>
#include <cstdint>
''', encoding='utf-8')
print(out)
