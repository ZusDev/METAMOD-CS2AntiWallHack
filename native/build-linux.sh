# Native Linux build. Run inside the Steam Runtime 3 SDK container for releases.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
if [[ "${1:-}" == "--install-tools" ]]; then
  apt-get update
  apt-get install -y binutils ca-certificates g++ git python3 python3-pip
fi
python3 native/setup.py
python3 -m pip install --target native/tools/ambuild 'git+https://github.com/alliedmodders/ambuild@d89ec91a7ac2607da07b50bb62346f9a10e9a998'
export PYTHONPATH="$repo/native/tools/ambuild${PYTHONPATH:+:$PYTHONPATH}"
export CC=gcc CXX=g++
mkdir -p native/obj
cd native/obj
python3 ../configure.py --sdks=cs2 --targets=x86_64 --enable-optimize --mms_path=../deps/metamod-source --hl2sdk-root=../deps --hl2sdk-manifests=../deps/metamod-source/hl2sdk-manifests
python3 -c 'from ambuild2.run import cli_run; cli_run()'
cd "$repo"
g++ -std=c++17 -I native/deps/json native/tests/rules_test.cpp -o native/tools/rules_test-linux
native/tools/rules_test-linux
python3 native/check_abi.py native/obj/package/cs2/addons/antiwallhack/bin/linuxsteamrt64/antiwallhack.so
python3 native/package.py --build native/obj/package/cs2
