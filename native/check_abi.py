"""Reject a Linux release exceeding Steam Runtime 3's ABI versions."""
from pathlib import Path
import re
import subprocess
import sys
path = Path(sys.argv[1])
text = subprocess.check_output(['readelf','--version-info',str(path)],text=True)
for family, limit in {'GLIBC':(2,31),'GLIBCXX':(3,4,28),'CXXABI':(1,3,12)}.items():
    maximum = max((tuple(map(int,v.split('.'))) for v in re.findall(rf'\b{family}_(\d+(?:\.\d+)+)\b',text)),default=())
    if maximum:
        print(f'{family}: {maximum}')
        if maximum > limit: raise SystemExit(f'{family} requirement exceeds Steam Runtime 3: {maximum}')
print('Steam Runtime 3 ABI version check passed.')
