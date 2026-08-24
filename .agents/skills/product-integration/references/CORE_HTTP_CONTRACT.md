# Core HTTP Contract Notes

Product 层依赖的动作字段：

- `index`: 当前 decision 内的 option index；step 后立即失效。
- `kind`: 如 `play_card` / `choose_row` / `choose_insert_position`。
- `source_object_index`, `target_object_index`: observation object index。
- `target_side`, `target_zone`, `target_row`: 结构化目标位置。
- `insert_position`: 排内动态插入位置；只有相关动作 >= 0。
- `stable_hash`: 调试/历史展示使用，不替代 `index`。

任何 fallback 文本解析只用于兼容旧 HTTP server，不应成为新 contract 的事实来源。
