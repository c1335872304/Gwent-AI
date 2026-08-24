# Golden trace 对拍工具

m14 固定了一个轻量 trace 协议：`gwent-golden-trace-v1`。

目标是：同一副牌、同一 seed、同一先手、同一动作脚本，Python core 和 C++ core 各输出一份 JSON trace，然后用 `compare_trace.py` 对比。

## C++ 输出 trace

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGWENT_BUILD_TRACE_TOOLS=ON
cmake --build build
./build/gwent_trace_runner --scenario smoke --seed 0 --starting-player 0 > cpp_trace.json
```

## 动作脚本格式

每行一个动作，空行和 `#` 注释会被忽略。

```text
play_hand p=0 h=0 row=melee side=self
play_hand p=1 h=0 row=ranged side=self
pass p=0
pass p=1
```

支持的动作：

- `pass p=0`
- `mulligan p=0 h=0`
- `play_hand p=0 h=0 row=melee side=self`
- `play_hand p=0 h=0 row=melee side=self position=1`（精确排内插入；省略时默认追加到排尾）
- `play_special p=0 h=0`
- `use_leader p=0 target=enemy:melee:0`
- `use_order p=0 source=self:melee:0 target=enemy:melee:0`
- `choose_target p=0 target=enemy:melee:0`

`side` 可用 `self`、`enemy`、`0`、`1`。目标引用格式是 `side:zone:index`，例如 `enemy:melee:0`。

## 对比两份 trace

```bash
python3 tools/golden/compare_trace.py python_trace.json cpp_trace.json --ignore-engine-name
```

如果 Python 和 C++ 的 entity id 分配暂时不同，可以先用：

```bash
python3 tools/golden/compare_trace.py python_trace.json cpp_trace.json --ignore-engine-name --ignore-entity-ids
```

等 entity 分配和随机洗牌完全对齐后，再去掉 `--ignore-entity-ids` 做严格对拍。

## 对拍节奏建议

1. 先只对拍 `setup`：先手、战术牌位置、手牌/牌库数量、换牌次数。
2. 再对拍无效果单位：出牌、分数、pass、小局结算。
3. 再对拍 pending target：动作后暂停、选择目标、恢复结算。
4. 最后逐步加入 Deck A 真实效果。

不要等所有卡牌都迁完再对拍；每迁 3–5 张牌，就加一条脚本。

## M16 shared semantic replay

`python_trace_exporter.py` now accepts the same semantic `.trace` lines used by
`gwent_trace_runner`.  Use `run_python_cpp_semantic_pair.py` to run both engines
against the first action-level shared cases:

```bash
python3 tools/golden/run_python_cpp_semantic_pair.py \
  --cpp-runner ./build/gwent_trace_runner \
  --python-core /path/to/GwentEnvCore_extracted \
  --out-dir build/golden/python_cpp_semantic
```

The initial semantic pair cases intentionally cover stable rules first: pass,
unit play, and play-then-pass.

## M39 trace protocol stabilization

m39 adds explicit schema docs and small command-line tools for trace/replay diagnostics:

- `TRACE_SCHEMA.md`
- `ACTION_SCRIPT_SCHEMA.md`
- `LEGAL_SURFACE_SCHEMA.md`
- `trace_diff.py`
- `legal_surface_diff.py`
- `seed_sweep.py`

Use legal surface output when reviewing AI/frontend-facing choices:

```bash
./build/gwent_trace_runner \
  --scenario trace_smoke \
  --script tools/golden/cases/trace_smoke.trace \
  --seed 0 \
  --starting-player 0 \
  --deck trace \
  --effects none \
  --include-legal-surface \
  > build/golden/trace_smoke.surface.json
```

Compare two full traces:

```bash
python3 tools/golden/trace_diff.py left.json right.json
```

Compare only embedded legal surfaces:

```bash
python3 tools/golden/legal_surface_diff.py left.json right.json
python3 tools/golden/legal_surface_diff.py left.json right.json --step 3
```

Sweep a script across multiple seeds and require deterministic repeat output:

```bash
python3 tools/golden/seed_sweep.py \
  --runner ./build/gwent_trace_runner \
  --script tools/golden/cases/trace_smoke.trace \
  --seeds 0-9 \
  --deck trace \
  --effects none \
  --include-legal-surface \
  --repeat-check
```
