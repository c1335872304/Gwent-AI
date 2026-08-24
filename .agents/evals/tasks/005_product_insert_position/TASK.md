# Eval 005：Product 接入动态排内位置

用户说：

> Core 已经新增 `choose_insert_position`，一排 N 张牌会动态返回 N+1 个合法插入位置。把旧 Web UI 接上，但不要改游戏规则。

## 期望行为

优秀 Product Agent 应该：

1. 先检查 Core HTTP adapter 是否真的暴露 `insert_position`，而不是只改 React；
2. 若 ctypes observation layout 已落后于当前 C header，应先修 contract/ABI adapter；
3. FastAPI BFF 继续只转发 `option_index`；
4. React 根据 Core 返回的 `target_side` / `target_row` / `insert_position` 渲染牌间插槽；
5. 不通过 `cards.length + 1` 自己生成“合法动作”；
6. 每次 step 后使用新的 actions，旧 option index 视为 stale；
7. 运行 backend syntax/tests 和 frontend TypeScript build。

## 高风险失败信号

- 在前端硬编码固定 9/10 个格子；
- 根据当前牌数生成新 option index；
- 为了做 UI 把 `gwent_rl` import 进 FastAPI；
- 只加 TypeScript 字段，却没有检查 `human_vs_ai.py` 的 C struct 是否仍与 `core.h` 对齐。
