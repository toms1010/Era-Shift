# Bug Tracker 报告

> 生成时间: 2026-09-30 · Phase 3 反馈系统（音频 / 动画 / 特效）

## 📊 统计概览

- 🔴 待处理: 0
- 🟡 进行中: 0
- 🟢 已解决: 8
- ⚪ 已关闭: 0
- 📝 总计: 8

本阶段发现的 8 个缺陷全部由新加入的测试捕获。它们共同的特征是：**编译通过、
运行正常、结果错误**。这类缺陷只能靠断言行为属性的测试发现，靠肉眼看不出来。

---

## 🟢 BUG-001 · 命中停顿吞掉了玩家输入

**严重度**: 高 · **状态**: 已解决

**现象** 玩家在一次重击后的 160ms 冻结期间按下 `Q`（时代切换）或 `E`（交互），
输入被静默丢弃。世界看起来"卡了一下"，然后什么都没发生。

**根因** `World::update` 在冻结时直接 `return`，跳过了整步——包括命令处理。
冻结本是表现层的装饰，却吃掉了玩家最重要的输入。

**修复** 冻结只拦截**没有按下任何键**的步骤：

```cpp
const bool pressed = commands.shiftPressed || commands.interactPressed ||
                     input.jumpPressed || input.dashPressed || input.attackPressed;
const bool frozen  = (m_hitStop > 0.0f) && !pressed;
```

按住方向键穿过冻结**仍然**会冻结，因为那正是"沉重"应该被感受到的地方。

**验证** 4 个 gameplay 测试失败后转为通过。

---

## 🟢 BUG-002 · `randomRange` 把有符号噪声当无符号用

**严重度**: 高 · **状态**: 已解决

**现象** Past 时代的树叶平均生成高度在屏幕**顶部之外**（y = −19），一半的环境
粒子出现在它们被生成的房间外面。

**根因** 粒子池的噪声源 `nextRandom()` 返回 `[-1, 1]`——散射速度时这正是需要的
形状。但 `randomRange(low, high)` 直接用它作区间比例：

```cpp
return low + (high - low) * system.nextRandom();   // 一半结果小于 low
```

**修复** 拆成两个意图明确的函数：

| 函数 | 用途 |
| --- | --- |
| `randomRange(system, low, high)` | 区间 `[low, high)`，内部重标定 |
| `randomSigned(system)` | `[-1, 1]`，用于速度和旋转 |

**验证** `TestParticles.cpp` 断言三个时代的环境粒子平均高度分别落在屏幕的
上半、中央和下半。

---

## 🟢 BUG-003 · 三个时代发出完全相同的环境粒子密度

**严重度**: 中 · **状态**: 已解决

**现象** 三个时代的环境粒子数完全相同（实测均为 120）。三种天气悄悄变成了
一种。

**根因** 60Hz 下"每秒 3 个"是每帧 0.05 个，取整后是 0；于是"每帧至少 1 个"
的下限接管了一切。速率参数被完全绕过。

**修复** 跨调用累计小数余额：

```cpp
m_ambientAccumulator += rate * dt;
int count = static_cast<int>(m_ambientAccumulator);
m_ambientAccumulator -= static_cast<float>(count);
```

同时保留每步上限（6），这样一次 1 秒的卡顿不会把整个预算花在已经迟到的那一帧上。

**结果** Past 13 / Present 8 / Future 21 个粒子，且与帧率无关。

**验证** `emission is frame-rate independent` 与
`the three eras emit visibly different weather` 两个测试。

---

## 🟢 BUG-004 · 攻击的视觉伸展从未发生，死亡从未淡出

**严重度**: 中 · **状态**: 已解决

**现象** 挥击时手臂不前伸（`armExtension` 恒为 0）；死亡动画结束时角色仍然
完全不透明（`alpha` 恒为 1）。

**根因** 动画表用了一个位置参数式的 `key()` 辅助函数，9 个 float 排成一列。
本该给 `armExtension` 的值落进了 `limbSpread`，本该给 `alpha` 的 0.0 落进了
`armExtension` 然后被默认值 1.0 覆盖。

**修复** 全部改用**指定初始化器**，字段名即文档：

```cpp
key(0.16f, (Rig{.scaleX = 1.06f, .scaleY = 0.94f, .offsetX = 2.0f,
                .lean = 12.0f, .armExtension = 1.0f}.pose()))
```

**验证** `a non-looping clip finishes and holds its last pose` 断言死亡片段
结束时 `alpha < 0.1`。

---

## 🟢 BUG-005 · 负帧数试图分配整个地址空间

**严重度**: 中 · **状态**: 已解决

**现象** 传入 −5 帧时抛出
`cannot create std::vector larger than max_size()`。

**根因** 先 resize 后检查：`out.assign(size_t(frames) * 2, 0)` 把有符号负数
转成接近 `SIZE_MAX` 的无符号数。

**修复** 检查移到 resize **之前**。`MusicSynth::render`、
`AmbienceSynth::render` 和 `renderSfx` 三处都是。

**为什么重要** 断点、暂停、或任何一帧耗时为零的时刻都会走到这些函数。

---

## 🟢 BUG-006 · 零长度包络不可达

**严重度**: 低 · **状态**: 已解决

**现象** 长度为 0 的 attack 在 t = 0 处返回 0 而不是 1。

**根因** `if (t <= 0.0f) return 0.0f;` 抢先返回，还没走到 `attack > 0.0f ?
t / attack : 1.0f` 那个分支。

**修复** 改为 `t < 0.0f`。

**后果** 修复前，游戏中每一个 click 都从一个静音采样开始。

---

## 🟢 BUG-007 · 攻击片段的标记与 `PlayerTuning` 不符

**严重度**: 中 · **状态**: 已解决

**现象** 火花在命中框**打开之前**出现。

**根因** 标记写在 0.08s 和 0.20s；实际的前摇结束于 0.06s，判定窗口关闭于
0.16s。两者相差 20–40ms——刚好是"看起来不对但说不出哪里不对"的量级。

**修复** 片段的关键时间现在**直接读自** `PlayerTuning`，并加了一个测试
`the attack clip's hit window matches the player's swing` 保持两者对齐。

**为什么重要** 这正是"模拟决定何时致命，动画被告知模拟在哪"这条规则的第一个
回报。图片和命中框是两个独立计时器的话，这个 bug 迟早会回来。

---

## 🟢 BUG-008 · `config/audio.json` 指向一个不存在的 `.wav`

**严重度**: 低 · **状态**: 已解决

**现象** `eraTransitionSfx: "sfx/era_shift/shift.wav"`。

**根因** 引用了一个项目从未有过的目录。

**修复** 配置文件现在描述实际发生的事：所有声音在运行时合成，音量对应混音器的
四个总线。移除了这个键。

**备注** 这类缺陷的特征是**永远不会触发**——因为没有代码读它。它是文档与
现实脱节的第一个证据，而它一直躺在配置里。

---

## 已检查且**未**发现问题的项

记录下来是为了说明检查过，不是为了凑数。

| 检查项 | 结果 |
| --- | --- |
| `MIX_Init()` 在无 timidity.cfg 的机器上失败 | 预期且非致命。合成的全是 PCM，日志明说 `PCM is unaffected`，混音器仍以 48kHz 启动 |
| 无声卡环境（CI / 容器） | 预期且非致命。`context().audio` 始终非空，`available()` 返回 false，游戏静音运行 |
| 粒子池耗尽 | 回收最旧的而非丢弃。满池时静默失去所有特效比丢一个火花糟糕得多 |
| 循环片段在 `setTime` 越界时回绕 | 挥击会回绕到末尾，在仍处于挥出状态时看起来已经收招。已改为钳制 |
| 长时间单步（1 秒） | 每个发射器都有每步上限，不会一次性请求数百个粒子 |
| 合成确定性 | 两次相同的调用产生逐位相同的结果，否则"火花看起来不对"的报告无法回答 |

---

## 建议的后续工作

1. **`graphics.maxParticles` 是死配置。** 它现在是 4000，而实际池是 768，且它
   是上限而非设置——调到 768 以上不会让游戏更繁忙。要么接上它，要么从配置里
   删掉。一个看起来有效但无效的设置，比没有这个设置更糟。
2. **玩家脚下音效的音高** 目前是速度的线性函数。踩在金属上和踩在草上是同一个
   声音。`TileKind` 已经有足够的信息可以区分。
3. **没有混音响度测试。** 测试断言了 `peak <= 1.0`，但一个恒定比峰值低 20dB 的
   音效会通过所有测试却在游戏里听不见。
