"""Fetch the pinned build dependencies. Requires Python 3 and Git."""
from pathlib import Path
import subprocess
import urllib.request
root = Path(__file__).resolve().parent
deps = root / 'deps'
deps.mkdir(exist_ok=True)
for name, url, branch, revision, recursive in [
    ('metamod-source','https://github.com/alliedmodders/metamod-source.git','master','9c49d4c9733d94605901fb80148ea8c553f059d3',True),
    ('hl2sdk-cs2','https://github.com/alliedmodders/hl2sdk.git','cs2','3ea5305b0efe980fdf32ab4c87782e3343e33a90',False),
]:
    path = deps/name
    if path.exists():
        print(f'Using existing {path}; ensure it matches documented revision {revision}.')
        continue
    subprocess.run(['git','clone','--branch',branch,url,str(path)],check=True)
    subprocess.run(['git','-C',str(path),'checkout',revision],check=True)
    if recursive: subprocess.run(['git','-C',str(path),'submodule','update','--init','--recursive'],check=True)
jsondir = deps/'json'
jsondir.mkdir(exist_ok=True)
if not (jsondir/'json.hpp').exists():
    urllib.request.urlretrieve('https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp',jsondir/'json.hpp')
print('Build dependencies ready.')
