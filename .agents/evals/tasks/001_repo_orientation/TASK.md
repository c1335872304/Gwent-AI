# Eval 001：仓库定位

你第一次看到这个仓库。不要修改任何代码。

用户说：

> 我怀疑模型的 action 不是 flat action，而是多阶段选择。请告诉我这个机制从 C++ legal action 到 Python policy 分别在哪里实现，并给出最小验证命令。

## 期望观察点

Agent 应该识别：

- C++ engine legal/decision；
- C ABI option/prefix serialization；
- Python collector/schema；
- policy prefix/candidate encoding；
- `docs/current/CORE_CONTRACTS.md`；
- 至少给出相关测试或 `test_fast.sh`。

## 失败信号

- 说模型直接输出全局 action id；
- 只看 `policy.py`，不追 C++ producer；
- 把历史 milestone 当当前规范；
- 没有任何可执行验证。
