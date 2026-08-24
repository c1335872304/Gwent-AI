# Training Tasks

`configs/training/` 描述算法参数，`training/tasks/` 描述一次可执行训练任务的生命周期。

```text
Training Task
  ├─ config
  ├─ initialization / resume policy
  ├─ game budget
  ├─ runtime
  ├─ checkpoint / eval
  └─ compatibility metadata
       │
       ▼
Python Orchestrator
       │
       ▼
gwent_rl.train_ppo
```

关键原则：

- warm-start 与 resume 不混用；
- source checkpoint 的 schema/grammar metadata 必须显式；
- 训练异常先排查 environment / collector / reward / GAE，再调优化参数；
- 正式产品只使用封盘 checkpoint，不依赖训练目录。

当前训练与 V3 设计见 [`../docs/current/TRAINING_AND_MODEL.md`](../docs/current/TRAINING_AND_MODEL.md)。
