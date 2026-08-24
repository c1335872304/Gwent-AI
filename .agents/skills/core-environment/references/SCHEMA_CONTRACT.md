# Schema Contract

## Ownership

| Contract | Owner | Current source |
|---|---|---|
| Observation / RL Schema version | Core | `config/rl_contract.json` |
| Action Grammar version | Core | `config/rl_contract.json` |
| Reward-config ABI version | Core | `config/rl_contract.json` |
| C generated mirror | Core | `include/gwent/c/rl_contract_versions.h` |
| Python generated mirror | Core | `python/src/gwent_rl/_contract_versions.py` |
| Observation dimensions/features | Core | `include/gwent/c/core.h` + `python/src/gwent_rl/schema.py` |
| Training Task Schema | Trainer | `python/src/gwent_rl/training/task.py` |

这些 contract 独立版本化，不联动 bump。

## 单一版本源

人工只维护 `config/rl_contract.json`。不要直接修改生成的 C/Python 版本文件。

```text
config/rl_contract.json
        │
        ├── scripts/bump_rl_contract.py
        │       ├── include/gwent/c/rl_contract_versions.h
        │       └── python/gwent_rl/_contract_versions.py
        │
        └── docs/current 只引用 manifest，不复制“当前 vN”
```

常见升级：

```bash
python scripts/bump_rl_contract.py --schema 8
python scripts/bump_rl_contract.py --grammar 4
python scripts/bump_rl_contract.py --reward 3
python scripts/bump_rl_contract.py --check
```

`check_schema.py` 会检查 manifest、两个生成 mirror、C/Python tensor 维度，以及 `docs/current/` 是否又写死了“当前 vN”。

## Observation schema bump

只有字段、维度或语义发生 breaking change 才 bump：

1. 修改真正的 observation 字段/语义和必要维度。
2. 运行 `python scripts/bump_rl_contract.py --schema <new>`。
3. 更新 `docs/current/CORE_CONTRACTS.md` 的**语义内容**；不需要到处改当前版本号。
4. 运行 `python scripts/check.py quick` / `test`。
5. 如果要复用旧 checkpoint，再由 Trainer 在 `training/migrations.py` **一处**明确加入允许的 warm-start migration，并由具体迁移 Task 声明 source/reset 语义。

普通 Training Task 不需要跟着改版本号：未显式写 `compatibility` 时默认跟随当前 environment contract。显式钉版本的迁移/兼容性任务可以保留 source/target metadata；真正 plan/run 时仍必须拒绝与目标 runtime 不匹配的组合。

新增 C ABI tensor/collector 字段且 policy 会消费它时，即使既有 feature dimension 常量没有变化，也属于
Observation schema breaking change。新增 decision kind、option kind 或顺序动作阶段属于 Action Grammar
breaking change。一次功能可能同时触发两者，但 Reward Config 仍需独立判断。

## 不要做

- 不全仓库替换 `vN → vN+1`。
- 不直接编辑 `rl_contract_versions.h` 或 `_contract_versions.py`。
- 不因为 observation bump 同时 bump Action Grammar / Reward Config / Training Task Schema。
- 不把 migration fixture 或 source checkpoint metadata 中的旧版本误判成当前 contract 漏改。
- 不自动假设旧 checkpoint 可以 warm-start 到新 schema；迁移兼容性必须显式审查。
