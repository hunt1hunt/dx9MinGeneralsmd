# MinGeneralsmd 项目修改场景示例

> 按需加载的修改示例文档。包含实际修改场景的可行步骤和注意事项。
> 主技能指令参见 `SKILL.md`。

---

## 场景 1：新增一种单位行为模块

### 目标
新增一种自定义 AI 行为模块，让某种单位在特定条件下自动触发特殊动作。

### 步骤

1. **定义 ModuleData 结构体**（.h 文件）
   - 在 `Include/GameLogic/` 下或对应的模块分类目录中新建或扩展头文件
   - 定义 `struct MyNewModuleData : public ModuleData`
   - 添加 INI 可解析的成员变量

2. **实现 FieldParse 表**
   ```cpp
   static const FieldParse MyNewModuleData::m_fieldParse[] = {
       { "MyParam",    INI::parseReal,    offsetof(MyNewModuleData, m_myParam), NULL },
       { "MyFlag",     INI::parseBool,    offsetof(MyNewModuleData, m_myFlag),  NULL },
       { 0, 0, 0, 0 }  // 终止符
   };
   ```

3. **实现 Module 类**
   - 继承自对应的模块接口（如 `UpdateModule`）
   - 实现 `update()` 方法（每帧调用）
   - 使用 `MAKE_STANDARD_MODULE_MACRO` 注册

4. **在 ThingTemplate 中挂载**
   - INI 配置：`Behavior = MyNewModule ModuleTag_01`
   - `ModuleTag_01` 下填写 `MyParam = value`

### 注意事项
- `ModuleData` 只能通过 `new` 分配，`Module` 通过 `new` + `MemoryPoolObject` 分配
- 确保在 `GameEngine.dsp` 中添加了新的 .cpp 文件
- 使用 `MAKE_STANDARD_MODULE_MACRO` 宏提供工厂注册

---

## 场景 2：修改 AI 逻辑

### 目标
修改 AI 的单位调度逻辑，让 AI 更聪明地组织进攻。

### 关注文件
- `Include/GameLogic/AI.h`（38KB）— 主 AI 系统头文件
- `Source/GameLogic/AI.cpp` — AI 实现
- `Include/GameLogic/GameLogic.h` — 需要通过 `TheGameLogic` 访问 AI

### 常用查找模式
```cpp
// 在 AI 系统中找到 Team 的调度逻辑
TheGameLogic->getAI()->getTeam(teamID)->doSomething();

// AI 模块的使用
AIUpdate *pAIUpdate = (AIUpdate*)->getObject()->getModule(NAME_AIUPDATE);
```

### 注意事项
- AI 代码位于 GameLogic 中，**必须保持确定性**
- 禁止使用随机数或系统时间作为决策依据（应使用确定性随机）
- 修改后需要验证录像回放能否同步

---

## 场景 3：添加新的 INI 配置字段

### 目标
为现有单位增加一个新的 INI 可配置属性。

### 步骤

1. **在对应的 ModuleData 中添加成员**
   ```cpp
   // 在 MyModuleData 中添加
   Real m_newParam;          // 新参数
   Bool m_newFlag;           // 新标志
   ```

2. **在构造函数中初始化**
   ```cpp
   MyModuleData::MyModuleData() :
       m_newParam(0.0f),
       m_newFlag(false)
   {}
   ```

3. **在 FieldParse 表中注册**
   ```cpp
   static const FieldParse MyModuleData::m_fieldParse[] = {
       // ... 原有字段
       { "NewParam",   INI::parseReal, offsetof(MyModuleData, m_newParam), NULL },
       { "NewFlag",    INI::parseBool, offsetof(MyModuleData, m_newFlag),  NULL },
       { 0, 0, 0, 0 }
   };
   ```

4. **在 INI 文件中使用**
   ```ini
   Behavior = MyModule ModuleTag_01
   ModuleTag_01 = MyModule
     NewParam = 5.0
     NewFlag = yes
   End
   ```

### 注意事项
- `FieldParse` 表必须以 `{ 0, 0, 0, 0 }` 终止
- 使用 `offsetof` 宏获取成员偏移量
- 需要重新编译所有依赖该 ModuleData 的代码

---

## 场景 4：修改网络协议消息

### 目标
新增一种网络命令类型，实现自定义客户端-服务器通信。

### 关注文件
- `Include/GameNetwork/NetCommandMsg.h`（16KB）— 命令消息定义
- `Source/GameNetwork/NetCommandMsg.cpp` — 实现
- `Include/GameNetwork/NetPacket.h`（12KB）— 数据包格式

### 步骤

1. **在 `GameMessage::Msg` 枚举中添加新类型**
2. **实现消息序列化（`xfer()` 方法）**
3. **在消息分发系统中添加处理分支**
4. **更新 `FrameData` 和 `FrameDataManager` 确保锁步同步**

### 注意事项
- 网络消息在 GameLogic 中处理，必须**保持确定性**
- 所有客户端必须收到**相同顺序**的消息
- 修改 `NetCommandMsg.h` 后需要重新编译整个 GameEngine 库

---

## 场景 5：修改渲染效果（W3D）

### 目标
修改某个单位的外观渲染效果，例如添加粒子特效或着色器效果。

### 关注文件
- `GameEngineDevice/Source/W3DDevice/` — W3D 渲染器实现
- `Include/GameClient/DrawModule.h` — 绘制模块接口
- `Source/GameClient/DrawModule.cpp` — 绘制模块实现
- `Include/Common/ModelState.h` — 模型状态管理

### 步骤

1. 在 DrawModule 中定位目标单位的绘制逻辑
2. 修改或新增 `DrawModule` 实现（例如添加粒子系统）
3. 在 INI 文件中的单位模板中关联新 DrawModule
4. 编译验证

### 注意事项
- W3D 是 DirectX 8 渲染器，不兼容 DirectX 9+ 的高级特性
- 着色器使用 NVASM 汇编格式（`.nvp` / `.nvv` 文件）
- 渲染代码在 GameClient 中，不需要确定性

---

## 场景 6：修改保存/加载系统（Xfer）

### 目标
为新模块或新数据结构添加保存/加载支持。

### 关注文件
- `Include/Common/Xfer.h` — 序列化协议基类
- `Include/Common/Snapshot.h` — 快照基类

### 示例
```cpp
// 在 Module 类中实现 xfer 方法
void MyModule::xfer(Xfer *pXfer) {
    // 调用基类
    Module::xfer(pXfer);

    // 序列化成员变量
    XferVersion currentVersion = 1;
    pXfer->xferVersion(&currentVersion);
    pXfer->xferReal(&m_myParam);
    pXfer->xferBool(&m_myFlag);
}
```

### 注意事项
- 所有需要保存/加载的类必须继承自 `Snapshot`
- 添加新版本号时保持向后兼容（读取旧版本存档）
- `XferSave` 和 `XferLoad` 使用相同的代码路径

---

## 场景 7：添加新的 GUI 界面

### 目标
在游戏中添加一个新的界面窗口或 HUD 元素。

### 关注文件
- `Include/GameClient/` 下的窗口和控件文件
- `GameWindowManager.h`, `GameWindow.h`, `ControlBar.h`

### 注意事项
- GUI 系统基于 MFC 风格的窗口层次结构
- 首先确认窗口资源定义（`.wnd` 文件或硬编码）
- 窗口事件通过消息流（`MessageStream`）传递
- 新建窗口需要在 `GameClient` 子系统初始化时注册

---

## 场景 8：修改地图编辑器（WorldBuilder）

### 目标
为 WorldBuilder 地图编辑器新增功能或修复问题。

### 关注目录
- `Tools/WorldBuilder/` — 整个 MFC 应用程序源码

### 注意事项
- WorldBuilder 是 **MFC 应用程序**，使用文档/视图架构
- 对 `.rc`（资源文件）和消息映射的修改要谨慎
- 必须理解 MFC 的消息映射宏（`ON_COMMAND`, `ON_MESSAGE` 等）
- 编译需要 MFC 库支持（VC6 自带）

---

## 修改提交前最终检查清单

对照以下项目逐一检查修改是否完备：

```
[ ] 1. 是否需要补充相关函数或类的声明和定义？
[ ] 2. 是否需要增加 #include 相关文件？
[ ] 3. 是否使用了 VC6.0 不支持的现代 C++ 语法？（auto, range-based for, std::shared_ptr, lambda 等）
[ ] 4. 变量声明是否在代码块顶部（C89 要求）？
[ ] 5. 是否已在 .dsp 项目中添加了新 .cpp 文件？
[ ] 6. INI FieldParse 表是否以 {0,0,0,0} 正确终止？
[ ] 7. Module 是否已通过 MAKE_STANDARD_MODULE_MACRO 注册？
[ ] 8. 如果是 GameLogic 修改：是否保持确定性？
[ ] 9. 如果是序列化修改：Xfer 版本号是否正确处理？
[ ] 10. 是否违反了无 RTTI 无异常的原则？
[ ] 11. 编译标志是否正确？（/W3 /WX 无警告）
[ ] 12. 预编译头 `PreRTS.h` 是否作为第一行 include？
