"""Package a native build, or explicitly a source-only archive."""
from pathlib import Path
import argparse
import zipfile
import shutil
root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path)
parser.add_argument('--source-only', action='store_true')
args = parser.parse_args()
if bool(args.build) == args.source_only:
    parser.error('Choose --build PATH or --source-only')
output = root.parent/'build'
output.mkdir(exist_ok=True)
files = []
platform = 'source'
if args.build:
    install = args.build.resolve()
    binaries = [p for p in install.glob('addons/antiwallhack/bin/*/antiwallhack.*') if p.suffix in ('.dll','.so')]
    if len(binaries) != 1: parser.error('Expected one native antiwallhack.dll or antiwallhack.so in the build package')
    platform = 'windows' if binaries[0].suffix == '.dll' else 'linux'
    # Package current configuration sources, even when reusing an existing binary staging folder.
    for source, destination in (
        (root/'AntiWallHack.cfg', install/'cfg/AntiWallHack.cfg'),
        (root/'gamedata.jsonc', install/'addons/antiwallhack/gamedata.jsonc'),
    ):
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.resolve() != destination.resolve():
            shutil.copy2(source, destination)
    for required in ('addons/metamod/antiwallhack.vdf','cfg/AntiWallHack.cfg','addons/antiwallhack/gamedata.jsonc'):
        if not (install/required).is_file(): parser.error(f'Missing {required}')
    files += [(p,p.relative_to(install).as_posix()) for folder in ('addons', 'cfg') for p in (install/folder).rglob('*') if p.is_file() and p.relative_to(install).as_posix() != 'addons/antiwallhack/config.jsonc']
names = ['AMBuilder','AMBuildScript','PackageScript','configure.py','setup.py','package.py','verify.py','inspect_elf.py','README.md','plugin-metadata.json','gamedata.jsonc','THIRD_PARTY.md']
names += ['exports.map','build-linux.sh','check_abi.py','AntiWallHack.cfg']
files += [(root/name,'source/AntiWallHack/native/'+name) for name in names]
files += [(p,'source/AntiWallHack/native/'+p.relative_to(root).as_posix()) for folder in ('src','tests') for p in (root/folder).rglob('*') if p.is_file() and '.pb.' not in p.name]
files += [(root.parent/name,'source/AntiWallHack/'+name) for name in ('README.md','LICENSE')]
files += [(root.parent/name,name) for name in ('README.md','LICENSE')]
target = output/f'AntiWallHack-Metamod-{platform}.zip'
with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as archive:
    for path,name in files: archive.write(path,name)
print(target)
