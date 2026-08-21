> [!IMPORTANT]
> 当前分支与上游差异过大(Layout, Editor)，目前停止维护 请移步至[DreamGUI](https://github.com/TypeDreamMoon/DreamGUI)获取最新版本


<p align="center">
  <img width="112" src="./Resources/Icon128.png" alt="LGUI logo">
</p>

<h1 align="center">LGUI</h1>

<p align="center">
  <strong>3D UI System for Unreal Engine</strong><br>
  Event Framework · Prefab Workflow · Tween Animation<br>
  事件框架 · 预制体工作流 · 补间动画
</p>

<p align="center">
  <a href="https://github.com/liufei2008/LGUI"><strong>Upstream Repository / 上游仓库</strong></a>
</p>

---

## Overview / 概览

| Capability | Description |
| --- | --- |
| 3D UI | UI rendering and interaction for Unreal Engine / 面向 Unreal Engine 的 UI 渲染与交互 |
| Event Framework | Reusable event dispatch and input workflow / 可复用的事件分发与输入流程 |
| Prefab Workflow | Hierarchical UI authoring with nested Prefabs and overrides / 支持嵌套 Prefab 与 Override 的层级化 UI 编辑 |
| Tween Animation | Tween-based UI motion and transitions / 基于补间的 UI 动画与过渡 |

## Prefab Workflow / Prefab 工作流

> [!IMPORTANT]
> The Prefab Editor works on a loaded preview hierarchy. Preview changes are separate from the serialized asset until **Apply** or **Save**.<br>
> Prefab Editor 修改的是已加载的预览层级；执行 **Apply** 或 **Save** 之前，预览修改尚未进入资产序列化数据。

| Action / 操作 | In-memory asset / 内存资产 | Package on disk / 磁盘 Package |
| --- | --- | --- |
| Edit preview / 编辑预览 | Unchanged / 不变 | Unchanged / 不变 |
| **Apply** | Serializes the complete preview hierarchy / 完整序列化预览层级 | Controlled by **Save on Apply**; default: **Never** / 由 **Save on Apply** 控制；默认：**Never** |
| **Save** | Applies the complete preview hierarchy / Apply 完整预览层级 | Saves the Prefab package / 保存 Prefab package |
| Runtime `LoadPrefab` | Creates an instance only / 仅创建实例 | Unchanged / 不变 |

### Nested Prefabs / 嵌套 Prefab

Saving a child Prefab refreshes its instances in loaded parent Prefabs. Registered parent overrides are restored after the child data is reloaded. Changes that were not recorded as overrides can be replaced during that refresh.

保存子 Prefab 后，已加载父 Prefab 中的对应实例会刷新；子 Prefab 数据重载后会恢复父级已登记的 Override。未被记录为 Override 的实例修改可能在刷新时被替换。

| Intent / 目的 | Edit here / 编辑位置 |
| --- | --- |
| Shared change / 所有实例共享 | Source child Prefab / 源子 Prefab |
| Parent-specific change / 仅当前父级生效 | Nested instance override / 嵌套实例 Override |

### Extension and Migration Safety / 扩展与迁移安全

> [!WARNING]
> `ULexUIPrefab::SavePrefab` and `ULexUIPrefabHelperObject::SavePrefab` perform **full Prefab serialization**, not a property-level patch. Calling `ClearLoadedPrefab` followed by `Init` rebuilds the loaded hierarchy and can discard unapplied editor changes.<br>
> `ULexUIPrefab::SavePrefab` 与 `ULexUIPrefabHelperObject::SavePrefab` 执行的是**完整 Prefab 序列化**，不是属性级补丁。调用 `ClearLoadedPrefab` 后再 `Init` 会重建已加载层级，可能丢弃尚未 Apply 的编辑器修改。

Prefab upgrade and migration tools must follow this checklist:

1. Block execution while the target Prefab Editor has unapplied changes, or use the existing dirty-editor confirmation path before refreshing it.<br>目标 Prefab Editor 存在未 Apply 修改时拒绝执行，或在刷新前使用现有的 dirty-editor 确认流程。
2. Build and validate changes on a transient duplicate before replacing target serialization data.<br>先在 Transient 副本上构建并验证修改，再替换目标资产的序列化数据。
3. Use source control or a backup, and report every Prefab package marked dirty.<br>修改前使用版本控制或备份，并明确报告所有被标记 dirty 的 Prefab package。
4. Do not reapply hard-coded hierarchy, slot, size, padding, or spacing values to an already-migrated Prefab.<br>不要对已迁移 Prefab 重复写入硬编码的层级、Slot、尺寸、Padding 或 Spacing。
5. Refresh loaded parent Prefabs after child changes while preserving registered overrides.<br>子 Prefab 修改后刷新已加载父 Prefab，同时保留已登记的 Override。
6. Use transient duplicates in automation tests; never call `SavePrefab` on production assets.<br>自动化测试必须使用 Transient 副本，禁止对正式资产调用 `SavePrefab`。

---

## License / 许可

MIT

Copyright (c) 2019-present, Lex Liu
