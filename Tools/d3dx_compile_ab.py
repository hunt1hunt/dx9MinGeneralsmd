import ctypes
import ctypes.wintypes as wt
import sys, os

d3dx = ctypes.WinDLL(r"C:\Windows\System32\d3dx9_42.dll")

D3DXCompileShader = d3dx.D3DXCompileShader
D3DXCompileShader.restype = ctypes.c_long
D3DXDisassembleShader = d3dx.D3DXDisassembleShader
D3DXDisassembleShader.restype = ctypes.c_long

class ID3DXBuffer(ctypes.Structure):
    pass

IID_ID3DXBuffer = (ctypes.c_byte * 16)(
    0x8f,0xba,0x2a,0x5d, ... ) if False else None

# We only need the buffer's vtable: GetBufferPointer (idx 3), GetBufferSize (4)
# Use comtypes-free manual COM: allocate pointer, call via ctypes.

def compile_src(src, profile=b"ps_2_a"):
    buf_out = ctypes.c_void_p()
    err_out = ctypes.c_void_p()
    data = src.encode('utf-8')
    hr = D3DXCompileShader(
        data, len(data),
        None, None,                       # no macros, no includes
        b"main", profile, 0,
        ctypes.byref(buf_out), ctypes.byref(err_out),
        None)
    errtext = b""
    if err_out.value:
        errtext = com_buffer_bytes(err_out.value)
    return hr, buf_out.value, errtext

def com_buffer_bytes(p):
    # COM: p -> vtable ptr -> func ptrs; GetBufferPointer=slot3, GetBufferSize=slot4
    vtbl = ctypes.cast(ctypes.c_void_p(p), ctypes.POINTER(ctypes.c_void_p))[0]
    funcs = ctypes.cast(vtbl, ctypes.POINTER(ctypes.c_void_p))
    GetBufferPointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(funcs[3])
    GetBufferSize = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(funcs[4])
    ptr = GetBufferPointer(ctypes.c_void_p(p))
    size = GetBufferSize(ctypes.c_void_p(p))
    return ctypes.string_at(ptr, size)

def disasm(code_bytes):
    buf = ctypes.c_void_p()
    hr = D3DXDisassembleShader(code_bytes, True, None, ctypes.byref(buf))
    if hr != 0 or not buf.value:
        return b"<disasm failed>"
    return com_buffer_bytes(buf.value)

def release(p):
    vtbl = ctypes.cast(ctypes.c_void_p(p), ctypes.POINTER(ctypes.c_void_p))[0]
    funcs = ctypes.cast(vtbl, ctypes.POINTER(ctypes.c_void_p))
    Release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(funcs[2])
    Release(ctypes.c_void_p(p))

if __name__ == "__main__":
    base = r"E:/Source/repos/MinGeneralsfreebuild2ok/GeneralsMD/Build/ps_src"
    for v in ["base", "noise1", "noise2", "noise12"]:
        o = open(os.path.join(base, v + "_orig.hlsl")).read()
        s = open(os.path.join(base, v + "_swap.hlsl")).read()
        hr_o, co, eo = compile_src(o)
        hr_s, cs_, es = compile_src(s)
        print(f"{v}: orig hr=0x{hr_o & 0xffffffff:08x} swap hr=0x{hr_s & 0xffffffff:08x}")
        if hr_o != 0:
            print("  orig ERR:", eo.decode('utf-8', 'replace')[:300])
        if hr_s != 0:
            print("  swap ERR:", es.decode('utf-8', 'replace')[:300])
        if hr_o == 0 and hr_s == 0:
            bo, bs = com_buffer_bytes(co), com_buffer_bytes(cs_)
            same = bo == bs
            print(f"  bytecode: orig {len(bo)}B swap {len(bs)}B  identical={same}")
            if not same:
                do, ds = disasm(bo), disasm(bs)
                open(os.path.join(base, v + "_orig.disasm"), 'wb').write(do)
                open(os.path.join(base, v + "_swap.disasm"), 'wb').write(ds)
                lo, ls = do.decode().splitlines(), ds.decode().splitlines()
                print(f"  disasm lines: orig {len(lo)} swap {len(ls)}")
                # count texld lines & differing lines
                tl_o = [l for l in lo if 'texld' in l]
                tl_s = [l for l in ls if 'texld' in l]
                print(f"  texld count: orig {len(tl_o)} swap {len(tl_s)}")
                dif = [(i, a, b) for i, (a, b) in enumerate(zip(lo, ls)) if a != b]
                print(f"  differing lines: {len(dif)}")
                for i, a, b in dif[:8]:
                    print(f"    L{i+1}: ORIG {a.strip()[:70]}")
                    print(f"    L{i+1}: SWAP {b.strip()[:70]}")
            release(co); release(cs_)
