"""SDK compile checks using the workspace's portable Zig compiler, not a release build."""
from pathlib import Path
import os
import subprocess
import sys
root = Path(__file__).resolve().parent
zig = root / 'tools/zig-windows-x86_64-0.13.0/zig.exe'
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(root/'tools/cache'), ZIG_LOCAL_CACHE_DIR=str(root/'tools/local-cache'))
common = ['-std=c++17', '-fms-extensions', '-Wno-switch', '-Wno-unused-value', '-DMETA_IS_SOURCE2', '-DSOURCE_ENGINE=25', '-DSE_CS2=25', '-DPLATFORM_64BITS']
includes = ['deps/metamod-source/core', 'deps/metamod-source/third_party/khook/include', 'deps/json', 'src']
includes += ['deps/hl2sdk-cs2/'+p for p in ['public','public/tier0','public/tier1','public/mathlib','public/entity2','public/engine','game/shared','game/server','common','thirdparty/protobuf-3.21.8/src']]
for p in includes: common += ['-I',(root/p).as_posix()]
linux = ['-DPOSIX','-DLINUX','-D_LINUX','-DX64BITS','-DNDEBUG','-DGAME_DLL','-DRAD_TELEMETRY_DISABLED','-DCOMPILER_GCC','-DGNUC','-Dstricmp=strcasecmp','-D_stricmp=strcasecmp','-D_snprintf=snprintf','-D_vsnprintf=vsnprintf']
def run(args):
    args = [(root/p).as_posix() if p.startswith(('src/','deps/','tools/','tests/')) else p for p in args]
    subprocess.run([str(zig), 'c++']+args, env=env, check=True, cwd=root)
if len(sys.argv) > 1 and sys.argv[1] == 'link-linux':
    run(['-target','x86_64-linux-gnu','-shared','-fPIC','-O2','-fvisibility=hidden']+common+linux+[
        'src/plugin.cpp','deps/hl2sdk-cs2/tier1/convar.cpp',
        'deps/hl2sdk-cs2/public/tier0/memoverride.cpp',
        'deps/hl2sdk-cs2/lib/linux64/mathlib.a','deps/hl2sdk-cs2/lib/linux64/interfaces.a',
        'deps/hl2sdk-cs2/lib/linux64/libtier0.so','-Wl,-z,defs','-Wl,--version-script='+str(root/'exports.map'),
        '-Wl,-rpath,$ORIGIN/../../../../../bin/linuxsteamrt64','-o','tools/antiwallhack.so'])
else:
    run(['-target','x86_64-linux-gnu','-c','-fPIC']+common+linux+['src/plugin.cpp','-o','tools/plugin-linux.o'])
    print('Linux SDK compile check passed.', flush=True)
    print('Windows plugin build requires the MSVC toolchain; unavailable on this machine.', flush=True)
    run(['-target','x86_64-windows-gnu','-std=c++17','-I','deps/json','tests/rules_test.cpp','-o','tools/rules_test.exe'])
    subprocess.run([str(root/'tools/rules_test.exe')], check=True, cwd=root)
