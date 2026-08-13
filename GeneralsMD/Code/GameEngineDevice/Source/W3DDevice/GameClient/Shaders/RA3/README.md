# Shaders/RA3 — W3X PBR shader 备份

本目录是**游戏运行时 shader 的 git 备份**，供版本控制和恢复用。

## 目录关系

- **游戏根目录**（运行时加载，**不在 git**）：`E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3\`
- **本目录**（git 备份）：`GeneralsMD\Code\GameEngineDevice\Source\W3DDevice\GameClient\Shaders\RA3\`

游戏启动时 `W3XEffectManager::GetEffect` 从游戏根目录读 .fx/.FXH 用 D3DX9 编译加载。**改 shader 改的是游戏根目录副本，本目录只做备份。**

## 使用方式

1. **改前先备份**：`sync_shaders.bat` 把游戏根 → 本目录拷一份并 commit。
2. **每次游戏内验证通过后**：再跑 `sync_shaders.bat` 同步 + commit。
3. 改错想回退：从 git 历史检出本目录文件拷回游戏根。

方向单向：游戏根 → 本目录。

## 关键文件

- `PBR5-10-objects-ARPBR.FX` — W3X PBR 主 shader（亮度/高光/金属度调参点）
- `w3x_soviet.fx` / `w3x_tread.fx` — W3X 入口（include ARPBR）
- `head1-maxui.FXH` / `head2-functions.FXH` / `head3-vsps.FXH` — PBR 函数/采样器
