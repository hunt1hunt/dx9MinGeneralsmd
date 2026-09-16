#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_laa.py - 给 32 位 RTS.exe 重打 LARGE_ADDRESS_AWARE 补丁（2GB -> 4GB）

背景:
    VC6 链接器的 /LARGEADDRESSAWARE 实际不生效, 每次重新构建 exe 后都必须
    重新运行本脚本, 否则又回到 2GB 地址空间, 长局会在 ~25min 真 OOM。

补丁原理 (PE 头字节补丁):
    e_lfanew = DOS 头偏移 0x3C 处的 DWORD, 指向 PE 签名 "PE\\0\\0"。
    COFF FileHeader 紧跟签名之后:
        +0   Machine         (2 字节, x86 = 0x14C)
        ...
        +18  Characteristics (2 字节, IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020)
    所以补丁位置 = e_lfanew + 4 + 18 = e_lfanew + 22, 只做 | 0x20。

    !!! 切勿改 e_lfanew + 4 的 Machine 字段 !!!
    上次踩坑: 把 Machine 误写成 0x8664 (AMD64), 启动报 WinError 193。

用法:
    python apply_laa.py <exe路径>
    python apply_laa.py "E:\\!!!!!!!QWCSB\\RTS.exe"
"""
import struct
import sys


def pe_characteristics(exe_path):
    """返回 (e_lfanew, machine, characteristics)。失败抛异常。"""
    with open(exe_path, "rb") as f:
        # DOS 头: 0x3C 处是 e_lfanew
        f.seek(0x3C)
        e_lfanew = struct.unpack("<I", f.read(4))[0]

        # PE 签名
        f.seek(e_lfanew)
        sig = f.read(4)
        if sig != b"PE\x00\x00":
            raise ValueError("不是有效的 PE 文件 (PE 签名不匹配): %s" % exe_path)

        # COFF FileHeader: Machine(+0), Characteristics(+18)
        f.seek(e_lfanew + 4 + 0)
        machine = struct.unpack("<H", f.read(2))[0]
        f.seek(e_lfanew + 4 + 18)
        characteristics = struct.unpack("<H", f.read(2))[0]
        return e_lfanew, machine, characteristics


def apply_laa(exe_path):
    e_lfanew, machine, characteristics = pe_characteristics(exe_path)
    off = e_lfanew + 4 + 18  # Characteristics 字段的文件偏移

    print("exe           : %s" % exe_path)
    print("e_lfanew      : 0x%X" % e_lfanew)
    print("Machine       : 0x%04X (%s)" % (machine,
          "x86" if machine == 0x14C else "AMD64" if machine == 0x8664 else "未知"))
    print("Characteristics: 0x%04X" % characteristics)

    if machine != 0x14C:
        print("警告: Machine != 0x14C (x86)，该 exe 可能不是 32 位 x86，补丁仍只改 Characteristics。")

    if characteristics & 0x0020:
        print("结果: 已带 LARGE_ADDRESS_AWARE (0x20)，无需重打。")
        return True

    with open(exe_path, "r+b") as f:
        f.seek(off)
        f.write(struct.pack("<H", characteristics | 0x0020))

    _, _, new_characteristics = pe_characteristics(exe_path)
    print("补丁已写入 -> Characteristics: 0x%04X" % new_characteristics)
    if new_characteristics & 0x0020:
        print("验证: LARGE_ADDRESS_AWARE 已置位 OK")
        return True
    print("验证失败: 0x20 位仍未置位")
    return False


def main():
    if len(sys.argv) != 2:
        print("用法: python apply_laa.py <exe路径>")
        return 2
    try:
        return 0 if apply_laa(sys.argv[1]) else 1
    except OSError as e:
        print("错误: 无法读写文件: %s" % e)
        return 1
    except ValueError as e:
        print("错误: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
