# 多脚本字幕渲染台 (Multilingual Subtitle Shaping Workbench)

面向国际展览影片的多脚本字幕编辑、双向排版与视频导出工具。重点支持阿拉伯语 /
希伯来语与拉丁数字、括号、品牌名的混排。

## 技术选型（成熟的文字成形与双向组件）

| 职责 | 组件 | 说明 |
|------|------|------|
| 双向算法 | **FriBidi** | UAX #9 嵌入层级、括号配对、重排映射 |
| 文字成形 | **HarfBuzz** | 阿拉伯连字 / 接合形态、组合附标定位 |
| 字形光栅化 | **FreeType** | 直接输出灰度位图，逐像素混合到画面 |
| 界面与显示 | **Qt 6 Widgets** | 逻辑文本编辑、bidi 检视、预览、工程管理 |
| 视频编码 | **FFmpeg**（外部静态构建） | PNG 帧序列 → H.264 mp4 |

关键约束：**代码中不存在任何字符串反转来模拟 RTL**。视觉顺序完全由
FriBidi 的 `map`（视觉槽 → 逻辑单元索引）决定，逻辑文本逐字节保存，
视觉顺序与原文索引分开存储。

## 目录结构

```
src/
  types.h/.cpp          数据结构、方向策略、JSON 序列化
  FontManager.h/.cpp    显式字体加载、cmap 覆盖表、按字符回退、缺字诊断
  TextShaper.h/.cpp     FriBidi 双向 + HarfBuzz 分段成形 + Unicode 断行
  SubtitleRenderer.h/.cpp  FreeType 光栅化、锚点定位、缺字拒绝导出
  ProjectStore.h/.cpp   工程保存/重开、布局数据导出
  VideoExporter.h/.cpp  帧渲染 + ffmpeg 编码
  PreviewWidget.*       实际字形预览（与导出同一渲染路径）+ 逻辑文本对照
  ControlCharModel.*    控制字符 / bidi 标记的可见检查
  MainWindow.*          编辑器、策略配置、字体管理、导出动作
tests/
  test_shaping.cpp      116 项成形/双向/断行/映射/选区/诊断/可复现性检查
  test_roundtrip.cpp    保存重开后逻辑文本逐字节不变
  export_cli.cpp        无头端到端视频导出（辅助用途，见下文图形入口）
  gui_smoke.cpp         offscreen 界面冒烟：真实编辑页混排编辑 + 点击/选区映射
scripts/
  env.sh build.sh test.sh
```

## 构建（无需 root）

第三方库与 Qt6 安装在仓库外的 `.deps/local`（通过下载 Debian deb 解压得到，
不污染系统）。

```bash
./scripts/build.sh     # 编译 GUI、测试、导出 CLI
./scripts/test.sh      # 运行全部检查并产出 output/subtitles.mp4
```

## 双向与成形管线

1. **逻辑单元化**：QString(UTF-16) → Unicode 码点单元，记录每个单元的逻辑索引。
2. **控制字符扫描**：C0/C1、bidi 控制符（LRE/RLE/LRO/RLO/LRI/RLI/…）、
   BOM、ZWNJ/ZWJ 全部进入可见诊断表，不静默剥离。
3. **FriBidi**：`fribidi_get_par_embedding_levels_ex`（带括号类型，参与 UAX #9
   N0）求嵌入层级；`fribidi_reorder_line` 用**预置为恒等置换的映射**产出视觉
   顺序（视觉槽 → 逻辑单元），并校验结果为合法置换。段落基准方向为
   0(LTR)/1(RTL)；断行后每一行用行内局部数组独立重排，行尾边界遵循 L1/L2，
   不会把下一逻辑行拉进本行。
4. **字体分段**：按 `FontManager` 的 cmap 覆盖与字重需求切分，缺覆盖即记
   明确诊断（`no loaded font covers U+XXXX`）。
5. **HarfBuzz 成形**：在字体段与层级变化的交点上再切分，每片只用一种方向
   成形（阿拉伯连字仅在 RTL 下激活）。使用 `MONOTONE_GRAPHEMES` cluster 级别，
   基字母与组合附标共享一个整形簇，并保留 cluster → 逻辑索引映射。
6. **括号镜像策略**：奇数层级按 UAX #9 L4 用镜像码点参与成形；
   `mirrorBrackets=false` 时用原码点，开关真正改变字形（有测试覆盖）。
7. **整形簇到源文字**：每个整形簇记录稳定的簇身份（起始码点单元）、完整源
   跨度（UTF-16 起止，连字覆盖全部字母、附标覆盖基字母）和视觉槽。选区高亮
   与命中测试都以簇为单位，因此不会只选中连字或组合标记的一部分。
8. **视觉定位**：按 FriBidi 映射把每个字形放入视觉槽，笔位只增不减，
   不反转任何字符串。
9. **断行**：Unicode 词边界（逻辑文本）映射到字形源索引，按测量行宽断行；
   长 token 硬断。重排只改变行归属，字形集合不变。

## 缺字处理（不静默出方框）

- `FontManager` 基于 cmap 判定覆盖，缺字写入 `ShapeDiagnostics.missing`。
- `SubtitleRenderer::renderFrame` 遇到缺字直接返回 `ok=false` 且**不绘制任何
  像素**；`VideoExporter` 在编码前对所有 cue 预检，任一缺字即中止导出。
- 预览中缺字以红色六边形 + 码点占位，明确可辨。

## 方向边界策略（可配置并写入布局数据）

`DirectionPolicy`：括号镜像、拉丁数字 LTR、货币随数字、中性标点归基准、
内联强调层级隔离。每个工程的生效策略随 `subtitles_layout.json` 落盘。

## 双向字幕的图形入口（主要入口）

双向字幕的编辑、检视与导出以 **GUI** 为主入口（`src/main.cpp` 启动
`MainWindow`）。命令行只用于无头辅助（见下一节），不承担编辑与核对。

1. **混排文字编辑** — 左侧「Logical text editor」。输入/粘贴阿拉伯语、
   希伯来语、拉丁数字、括号与品牌名混排文本；文本按逻辑顺序原样保存，
   预览绝不修改或反转原串。
2. **段落方向** — 左侧「Paragraph direction」下拉框：Auto（首个强字符）/
   Force LTR / Force RTL。
3. **方向边界策略** — 右侧「Direction boundary policy」复选框：括号镜像、
   数字 LTR、货币随数字、中性标点归基准、强调隔离。改动立即重新成形。
4. **字形预览** — 中间预览区绘制真实 FreeType 字形（与导出同一渲染路径），
   顶部标注计算出的段落基准层级，底部逻辑文本条逐字显示存储原文，并注明
   暖色 = RTL 层级、中性色 = LTR 层级。
5. **在字形上选择/编辑** — 在预览中点击或拖拽会命中所点击的字形所对应的
   整形簇，并把**逻辑字符区间**回填到左侧编辑器；反向地，在编辑器中选中
   逻辑文字会在预览中高亮其对应的视觉单元。连字与基字母+附标始终整簇选中，
   混排中的数字按视觉 LTR 顺序高亮。
6. **映射异常提示** — 预览下方诊断条显示基准层级、视觉行数、字形数、控制
   字符表，以及「Logical/visual mapping warnings (selection may be
   unreliable)」。映射校验失败会在此明确列出，而不是用默认值掩盖。
7. **控制字符检查** — 下方「Control / bidi characters」表列出 C0/C1、bidi
   控制符、BOM、ZWNJ/ZWJ 的逻辑索引与码点。
8. **导出** — 右栏「Export video + layout…」；存在缺字时导出被阻止，绝不
   静默输出方框。

验证方式（离屏，可复跑）：

```bash
./tests/build-gui/gui_smoke /tmp/gui.png
```

该冒烟测试加载真实 `MainWindow`，在编辑页写入混排样例，随后按同一渲染几何
点击逻辑索引 10 的字形单元，断言编辑器选区为 `10..11`、预览选区一致；点击
连字 `لا` 时断言整簇 `0..2` 一起选中；并断言逻辑 `2026` 映射到 4 个字形的
视觉 LTR 高亮。

## 命令行（仅辅助）

`tests/export_cli.cpp` 是无头辅助工具，用于在 CI 中生成成片与布局数据，不提供
交互式编辑或映射核对；这些由上述 GUI 入口完成。

```bash
./tests/build-export/export_cli -o out.mp4 -l out_layout.json --ffmpeg "$FFMPEG"
```

## 预览/导出一致性

`PreviewWidget` 与 `VideoExporter` 共享同一个 `FontManager` 实例与
`SubtitleRenderer` 代码路径、同一份字体文件集合与方向参数，杜绝预览与成片
字形不一致。预览的点击/拖拽命中、选区高亮与布局 JSON 中的视觉槽都来自同一份
`ShapedParagraph::clusters`（簇身份 + 显示位置），是修补后的实际计算，不是
默认值兜底。

## 可复现成形与映射证据

两次成形产出相同的 glyph id、源索引、坐标（测试 `reproducible-shaping`）。
布局 JSON 同时保存逻辑文本、bidi 层级、段落视觉映射版本
（`logicalVisualMapping`：`mappingVersion`、`clusterLevel`、
`bidiBaseLevel`、`clusterCount`、`warnings`），以及视觉行内每个整形簇与字形
的 `clusterId / sourceStart / sourceEnd / visualSlot / bidiLevel /
glyphId / x / y / advance / fontId`，可独立复核「逻辑字符 ↔ 实际字形」。
`tests/test_shaping.cpp` 中的映射与选区回归（`bidi-map-is-permutation`、
`cluster-source-spans-cover-logical-text`、`rtl-line-leftmost-is-logically-last`、
`selection-keeps-ligature-together`、`digits-highlighted-in-visual-ltr-order` 等）
把簇身份和显示位置对应到可执行的断言，失败时不会回退到恒等或默认映射而静默
通过。
