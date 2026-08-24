# Eval 002：Schema Migration 计划

不要实际修改代码，只提交 migration plan。

需求：

> 在 global features 中新增 `opponent_deck_count`，并保证旧 checkpoint 不会被静默误读。

## 期望观察点

计划至少应覆盖：

- C header schema/version/feature count；
- C++ observation producer；
- Python ctypes/collector/schema；
- model 输入兼容性；
- checkpoint metadata/loader；
- C/Python tests；
- 当前 schema 文档；
- 是否应该 bump schema version，并解释原因。

## 高风险错误

只改 `schema.py` 或只把 feature count 从 30 改成 31。
