/**
 * SPM_to_PBR.jsx — Adobe Photoshop Script
 * ==========================================
 *
 * 将 RA3 日冕 mod 的 SPM 贴图转换为 MinGenerals _pbr.dds 格式
 *
 * 通道映射:
 *   SPM.R (光滑度) → _pbr.R (粗糙度, 取反)
 *   SPM.G (金属度)  → _pbr.G (金属度, 直接拷贝)
 *   独立 AO 贴图    → _pbr.B (环境光遮蔽)
 *
 * 使用方法:
 *   1. 在 Photoshop 中: 文件 → 脚本 → 浏览 → 选择此文件
 *   2. 首先选择 SPM 贴图文件
 *   3. 然后选择 AO 贴图文件
 *   4. 脚本自动生成 _pbr.dds 的 PSD 文件
 *   5. 手动保存为 DDS (DXT5, sRGB=OFF, 生成 MIPMAP)
 *
 * 要求:
 *   - Photoshop CS6 或更高版本
 *   - SPM 和 AO 贴图应为相同分辨率 (512x512 或 1024x1024)
 *
 * @author  MinGenerals PBR Pipeline
 * @version 1.0
 */

// ============================================================
// 选择输入文件
// ============================================================

// 选择 SPM 贴图
var spmFile = File.openDialog(
    "选择 SPM 贴图文件 (日冕格式, 如 unit_body_spm.dds)",
    "*.dds;*.tga;*.png;*.jpg;*.psd"
);
if (!spmFile) {
    alert("未选择 SPM 文件，脚本终止。");
    exit();
}

// 选择 AO 贴图
var aoFile = File.openDialog(
    "选择 AO 贴图文件 (环境光遮蔽, 如 unit_body_ao.dds)",
    "*.dds;*.tga;*.png;*.jpg;*.psd"
);
if (!aoFile) {
    alert("未选择 AO 文件，脚本终止。");
    exit();
}

// 选择输出保存位置
var outputFile = File.saveDialog(
    "保存为 PSD 文件 (然后手动另存为 DXT5 DDS)",
    "unit_body_pbr.psd"
);
if (!outputFile) {
    alert("未选择输出路径，脚本终止。");
    exit();
}

// ============================================================
// 打开源文件
// ============================================================

// 记录当前活跃文档
var originalDoc = activeDocument;

// 打开 SPM 和 AO
var spmDoc, aoDoc;
try {
    spmDoc = open(spmFile);
} catch (e) {
    alert("无法打开 SPM 文件:\n" + e.toString());
    exit();
}

try {
    aoDoc = open(aoFile);
} catch (e) {
    alert("无法打开 AO 文件:\n" + e.toString());
    spmDoc.close(SaveOptions.DONOTSAVECHANGES);
    exit();
}

// 获取尺寸
var docWidth = spmDoc.width;
var docHeight = spmDoc.height;

// 检查尺寸一致性
if (aoDoc.width !== docWidth || aoDoc.height !== docHeight) {
    alert("警告: SPM 和 AO 贴图尺寸不一致!\n" +
          "SPM: " + docWidth + "x" + docHeight + "\n" +
          "AO: " + aoDoc.width + "x" + aoDoc.height + "\n\n" +
          "将以 SPM 尺寸为准。");
}

// ============================================================
// 提取通道数据
// ============================================================

// --- 从 SPM 提取 R 通道 (光滑度) ---
// 复制 SPM 的红色通道到新文档
var spmRedChan = spmDoc.channels[0]; // 0=Red
spmRedChan.duplicate();

// duplicate() 创建了一个新文档，获取它
var tempRedDoc = activeDocument;
tempRedDoc.channels[0].name = "SPM_R";

// 反相: 光滑度 → 粗糙度 (Roughness = 1 - Smoothness)
// 使用反相调整图层
var invertLayer = tempRedDoc.artLayers.add();
invertLayer.name = "Invert";
invertLayer.kind = LayerKind.SOLIDFILL; // 先加一个调整图层
invertLayer.remove(); // 移除，我们用更直接的方式

// 更可靠的方式: 使用 Invert 调整
var adjLayer = tempRedDoc.artLayers.add();
adjLayer.kind = LayerKind.NORMAL;
// 使用调整命令
tempRedDoc.selection.selectAll();
// 复制当前内容
tempRedDoc.selection.copy();
// 粘贴为新图层
var pasteLayer = tempRedDoc.artLayers.add();
tempRedDoc.paste();
// 应用反相
pasteLayer.adjustments.invert();
// 合并可见图层
tempRedDoc.mergeVisibleLayers();
tempRedDoc.channels[0].name = "Roughness";

// --- 从 SPM 提取 G 通道 (金属度) ---
var spmGreenChan = spmDoc.channels[1]; // 1=Green
spmGreenChan.duplicate();
var tempGreenDoc = activeDocument;
tempGreenDoc.channels[0].name = "Metalness";

// --- 从 AO 提取灰度作为 AO ---
aoDoc.channels[0].duplicate(); // 使用 RGB 合成的灰度 Channel 0
var tempAoDoc = activeDocument;
tempAoDoc.channels[0].name = "AO";

// ============================================================
// 合并为 _pbr.dds 三通道文档
// ============================================================

// 创建新的 RGB 文档
var pbrDoc = app.documents.add(
    docWidth, docHeight,
    72,                       // 分辨率
    "pbr_output",
    NewDocumentMode.RGB,
    DocumentFill.WHITE,
    1                         // 1 位 alpha — 不需要
);

// 定义通道映射:
// R = Roughness (来自 SPM.R 的反相)
// G = Metalness (来自 SPM.G)
// B = AO (来自 AO 贴图的灰度)

// 复制 Roughness 到 R 通道
app.activeDocument = tempRedDoc;
tempRedDoc.selection.selectAll();
tempRedDoc.selection.copy();
app.activeDocument = pbrDoc;
pbrDoc.channels[0].paste(); // 粘贴到 Red 通道

// 复制 Metalness 到 G 通道
app.activeDocument = tempGreenDoc;
tempGreenDoc.selection.selectAll();
tempGreenDoc.selection.copy();
app.activeDocument = pbrDoc;
pbrDoc.channels[1].paste(); // 粘贴到 Green 通道

// 复制 AO 到 B 通道
app.activeDocument = tempAoDoc;
tempAoDoc.selection.selectAll();
tempAoDoc.selection.copy();
app.activeDocument = pbrDoc;
pbrDoc.channels[2].paste(); // 粘贴到 Blue 通道

// ============================================================
// 清理临时文档
// ============================================================

tempRedDoc.close(SaveOptions.DONOTSAVECHANGES);
tempGreenDoc.close(SaveOptions.DONOTSAVECHANGES);
tempAoDoc.close(SaveOptions.DONOTSAVECHANGES);
spmDoc.close(SaveOptions.DONOTSAVECHANGES);
aoDoc.close(SaveOptions.DONOTSAVECHANGES);

// ============================================================
// 保存输出
// ============================================================

// 保存为 PSD
var psdOptions = new PhotoshopSaveOptions();
psdOptions.alphaChannels = false;
psdOptions.layers = false;
psdOptions.spotColors = false;
psdOptions.embedColorProfile = false;

pbrDoc.saveAs(outputFile, psdOptions, true, Extension.LOWERCASE);

// ============================================================
// 导出提示
// ============================================================

var ddsName = outputFile.name.replace(/\.psd$/i, "_pbr.dds");

alert(
    "转换完成!\n\n" +
    "文件已保存为:\n" + outputFile.fsName + "\n\n" +
    "下一步: 在 Photoshop 中打开此文件，然后:\n" +
    "1. 文件 → 另存为\n" +
    "2. 格式选择: DDS (*.dds)\n" +
    "3. DDS 导出设置:\n" +
    "   - 格式: DXT5 (BC3)\n" +
    "   - sRGB: 关闭 (重要!)\n" +
    "   - MIP 链: 生成\n" +
    "   - 文件名: " + ddsName + "\n\n" +
    "然后放入 GeneralsMD/Data/Art/Textures/ 对应目录"
);

// 将输出文档设为活跃
app.activeDocument = pbrDoc;
