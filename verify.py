"""SDK compile checks using the workspace's portable Zig compiler, not a release build."""

from pathlib import Path
import os
import struct
import subprocess
import sys

root = Path(__file__).resolve().parent
subprocess.run([sys.executable, str(root / "generate_defaults.py")], check=True)
zig = root / "tools/zig-windows-x86_64-0.13.0/zig.exe"
env = dict(
    os.environ, ZIG_GLOBAL_CACHE_DIR=str(root / "tools/cache"), ZIG_LOCAL_CACHE_DIR=str(root / "tools/local-cache")
)
common = [
    "-std=c++17",
    "-fms-extensions",
    "-Wno-switch",
    "-Wno-unused-value",
    "-DMETA_IS_SOURCE2",
    "-DSOURCE_ENGINE=25",
    "-DSE_CS2=25",
    "-DPLATFORM_64BITS",
]
# STL/async objects stay on the C++ heap; SDK containers keep their explicit allocator.
common += ["-DNO_MEMOVERRIDE_NEW_DELETE"]
includes = ["deps/metamod-source/core", "deps/metamod-source/third_party/khook/include", "deps/json", "src"]
includes += [
    "deps/hl2sdk-cs2/" + p
    for p in [
        "public",
        "public/tier0",
        "public/tier1",
        "public/mathlib",
        "public/entity2",
        "public/engine2",
        "game/shared",
        "game/server",
        "common",
        "thirdparty/protobuf-3.21.8/src",
    ]
]
for p in includes:
    common += ["-I", (root / p).as_posix()]
linux = [
    "-DPOSIX",
    "-DLINUX",
    "-D_LINUX",
    "-DX64BITS",
    "-DNDEBUG",
    "-DGAME_DLL",
    "-DRAD_TELEMETRY_DISABLED",
    "-DCOMPILER_GCC",
    "-DGNUC",
    "-Dstricmp=strcasecmp",
    "-D_stricmp=strcasecmp",
    "-D_snprintf=snprintf",
    "-D_vsnprintf=vsnprintf",
]


def run(args):
    args = [(root / p).as_posix() if p.startswith(("src/", "deps/", "tools/", "tests/")) else p for p in args]
    subprocess.run([str(zig), "c++"] + args, env=env, check=True, cwd=root)


def dynamic_symbols(path):
    """Yield (name, bind, type, defined, version index, size) for every .dynsym entry."""
    data = Path(path).read_bytes()
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shsize, shcount = struct.unpack_from("<HH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    versions = next((struct.unpack_from(f"<{s[5] // 2}H", data, s[4]) for s in sections if s[1] == 0x6FFFFFFF), None)
    for s in (s for s in sections if s[1] == 11):
        st = sections[s[6]]
        table = data[st[4] : st[4] + st[5]]
        for index, offset in enumerate(range(0, s[5], s[9])):
            name, info, other, section, value, size = struct.unpack_from("<IBBHQQ", data, s[4] + offset)
            symbol = table[name : table.index(b"\0", name)].decode()
            yield symbol, info >> 4, info & 15, section != 0, versions[index] & 0x7FFF if versions else 1, size


def tier0_stub(probe, sdk_tier0, output):
    """Build a link-only libtier0.so exporting just the unversioned symbols a tier0-less probe still imports."""
    exported = {
        name: (kind, size) for name, bind, kind, defined, version, size in dynamic_symbols(sdk_tier0) if defined
    }
    needed = sorted(
        {
            n
            for n, bind, kind, defined, version, size in dynamic_symbols(probe)
            if n and not defined and bind == 1 and version <= 1
        }
    )
    missing = [name for name in needed if name not in exported]
    if missing:
        raise SystemExit("Plugin imports symbols libtier0.so does not export: " + ", ".join(missing))
    output.mkdir(parents=True, exist_ok=True)
    (output / "libtier0.c").write_text(
        "".join(
            f"char {n}[{max(exported[n][1], 1)}];\n" if exported[n][0] == 1 else f"void {n}(void) {{}}\n"
            for n in needed
        )
    )
    command = ["cc", "-target", "x86_64-linux-gnu", "-shared", "-fPIC", "-w", "-Wl,-soname,libtier0.so"]
    subprocess.run(
        [str(zig)] + command + [str(output / "libtier0.c"), "-o", str(output / "libtier0.so")], env=env, check=True
    )
    return output / "libtier0.so"


sources = sorted(p.relative_to(root).as_posix() for p in (root / "src").rglob("*.cpp"))
if len(sys.argv) > 1 and sys.argv[1] == "link-linux":
    link = (
        ["-target", "x86_64-linux-gnu", "-shared", "-fPIC", "-O2", "-fvisibility=hidden"]
        + common
        + linux
        + sources
        + [
            "deps/hl2sdk-cs2/tier0_perproject/convar.cpp",
            "deps/hl2sdk-cs2/public/tier0/memoverride.cpp",
            "deps/hl2sdk-cs2/lib/linux64/mathlib.a",
            "deps/hl2sdk-cs2/lib/linux64/interfaces.a",
            "-Wl,--version-script=" + str(root / "exports.map"),
            "-Wl,-rpath,$ORIGIN/../../../../../bin/linuxsteamrt64",
        ]
    )
    run(link + ["-o", "tools/tier0-probe.so"])
    stub = tier0_stub(
        root / "tools/tier0-probe.so", root / "deps/hl2sdk-cs2/lib/linux64/libtier0.so", root / "tools/stubs"
    )
    run(link + [stub.as_posix(), "-Wl,-z,defs", "-o", "tools/antiwallhack.so"])
else:
    if len(sys.argv) == 1 or sys.argv[1] != "tests":
        (root / "tools/objects").mkdir(exist_ok=True)
        for source in sources:
            output = "tools/objects/" + source.replace("/", "_") + ".o"
            run(["-target", "x86_64-linux-gnu", "-c", "-fPIC"] + common + linux + [source, "-o", output])
        print("Linux SDK compile check passed.", flush=True)
    for name in (
        "rules_test",
        "smoke_test",
        "targeting_test",
        "vis_test",
        "smoke_visibility_test",
        "m_visibility_test",
        "gamedata_test",
        "update_policy_test",
        "map_update_test",
    ):
        run(
            [
                "-target",
                "x86_64-linux-gnu",
                "-std=c++17",
                "-I",
                "deps/json",
                "-I",
                "src",
                f"tests/{name}.cpp",
                "-o",
                f"tools/{name}-linux",
            ]
        )
        if sys.platform == "linux":
            subprocess.run([str(root / f"tools/{name}-linux")], check=True, cwd=root)
        else:
            print(f"{name}: Linux executable compiled; execution requires Linux.", flush=True)

    # Portable policy tests can run on the build host without compiling a plugin for that host.
    if sys.platform == "win32":
        for name in (
            "targeting_test",
            "vis_test",
            "rules_test",
            "smoke_visibility_test",
            "m_visibility_test",
            "gamedata_test",
            "update_policy_test",
            "map_update_test",
        ):
            run(
                [
                    "-target",
                    "x86_64-windows-gnu",
                    "-std=c++17",
                    "-I",
                    "deps/json",
                    "-I",
                    "src",
                    f"tests/{name}.cpp",
                    "-o",
                    f"tools/{name}-host-test.exe",
                ]
            )
            subprocess.run([str(root / f"tools/{name}-host-test.exe")], check=True, cwd=root)
