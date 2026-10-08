"""Check the ELF architecture, loader dependencies and Metamod export without Linux."""
from pathlib import Path
import struct
import re
root = Path(__file__).resolve().parent
data = (root/'tools/antiwallhack.so').read_bytes()
assert data[:5] == b'\x7fELF\x02' and struct.unpack_from('<H',data,18)[0] == 62
shoff = struct.unpack_from('<Q',data,40)[0]
shsize, shcount = struct.unpack_from('<HH',data,58)
sections = [struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize) for i in range(shcount)]
def string(table,index): return table[index:table.index(b'\0',index)].decode()
exports = []
needed = []
versions = []
for s in sections:
    if s[1] == 11:
        st = sections[s[6]]
        table = data[st[4]:st[4]+st[5]]
        for i in range(0,s[5],s[9]):
            name, info, other, section, value, size = struct.unpack_from('<IBBHQQ',data,s[4]+i)
            assert info>>4 != 10, 'GNU unique symbols would prevent unloading'
            if section and info>>4 in (1,2): exports.append(string(table,name))
    if s[1] == 6:
        st = sections[s[6]]
        table = data[st[4]:st[4]+st[5]]
        for i in range(0,s[5],16):
            tag,value = struct.unpack_from('<qQ',data,s[4]+i)
            if tag == 1: needed.append(string(table,value))
    if s[1] == 0x6ffffffe:
        st = sections[s[6]]
        table = data[st[4]:st[4]+st[5]]
        offset = s[4]
        while True:
            version,count,file,aux,next_entry = struct.unpack_from('<HHIII',data,offset)
            aux_offset = offset+aux
            for i in range(count):
                hash_value,flags,other,name,next_aux = struct.unpack_from('<IHHII',data,aux_offset)
                versions.append(string(table,name))
                aux_offset += next_aux
            if not next_entry: break
            offset += next_entry
assert 'CreateInterface' in exports
assert set(exports) == {'CreateInterface'}, 'Unexpected public static runtime exports'
assert all('/' not in p and '\\' not in p for p in needed), needed
print('ELF x86-64 / CreateInterface export verified.')
print('Loader dependencies:', ', '.join(needed))
print('GNU unique symbols absent; static runtime exports hidden.')
for family,limit in {'GLIBC':(2,31),'GLIBCXX':(3,4,28),'CXXABI':(1,3,12)}.items():
    found = [tuple(map(int,m.group(1).split('.'))) for v in versions if (m := re.fullmatch(family+r'_(\d+(?:\.\d+)+)',v))]
    if found:
        maximum = max(found)
        assert maximum <= limit, f'{family} exceeds SteamRT3: {maximum}'
        print(f'{family} maximum requirement: {maximum}')
