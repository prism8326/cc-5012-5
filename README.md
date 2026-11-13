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
  test_shaping.cpp      97 项成形/双向/断行/诊断/可复现性检查
  test_roundtrip.cpp    保存重开后逻辑文本逐字节不变
  export_cli.cpp        无头端到端视频导出
  gui_smoke.cpp         offscreen 界面截图冒烟
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
3. **FriBidi**：`fribidi_get_par_embedding_levels_ex` 求嵌入层级；
   `fribidi_reorder_line` 产出视觉映射。段落基准方向为 0(LTR)/1(RTL)，
   断行后每行依据该基准重新对齐。
4. **字体分段**：按 `FontManager` 的 cmap 覆盖与字重需求切分，缺覆盖即记
   明确诊断（`no loaded font covers U+XXXX`）。
5. **HarfBuzz 成形**：按段内 bidi 层级选择 RTL/LTR 成形方向（阿拉伯连字仅在
   RTL 下激活），保留 HarfBuzz cluster 到逻辑索引的映射。
6. **括号镜像策略**：奇数层级按 UAX #9 L4 用镜像码点参与成形；
   `mirrorBrackets=false` 时用原码点，开关真正改变字形（有测试覆盖）。
7. **视觉定位**：按 FriBidi 映射把每个字形放入视觉槽，笔位只增不减，
   不反转任何字符串。
8. **断行**：Unicode 词边界（逻辑文本）映射到字形源索引，按测量行宽断行；
   长 token 硬断。重排只改变行归属，字形集合不变。

## 缺字处理（不静默出方框）

- `FontManager` 基于 cmap 判定覆盖，缺字写入 `ShapeDiagnostics.missing`。
- `SubtitleRenderer::renderFrame` 遇到缺字直接返回 `ok=false` 且**不绘制任何
  像素**；`VideoExporter` 在编码前对所有 cue 预检，任一缺字即中止导出。
- 预览中缺字以红色六边形 + 码点占位，明确可辨。

## 方向边界策略（可配置并写入布局数据）

`DirectionPolicy`：括号镜像、拉丁数字 LTR、货币随数字、中性标点归基准、
内联强调层级隔离。每个工程的生效策略随 `subtitles_layout.json` 落盘。

## 预览/导出一致性

`PreviewWidget` 与 `VideoExporter` 共享同一个 `FontManager` 实例与
`SubtitleRenderer` 代码路径、同一份字体文件集合与方向参数，杜绝预览与成片
字形不一致。

## 可复现成形

两次成形产出相同的 glyph id、源索引、坐标（测试 `reproducible-shaping`）。
布局 JSON 同时保存逻辑文本、bidi 层级、视觉行内每个字形的 glyphId /
sourceIndex / x / y / advance / fontId，可独立复核。
