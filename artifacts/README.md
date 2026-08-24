# Model Artifacts

`artifacts/` 保存需要被训练或迁移流程精确引用的 pinned 模型资产；普通训练输出仍写入 `runs/`。

原则：

- `artifacts/checkpoints/registry.yaml` 是 checkpoint 资产索引；
- warm-start source 应固定路径、SHA-256 与 source contract metadata；
- 文件名不是身份，hash 才是稳定身份边界；
- checkpoint compatibility 必须显式验证，不能把不同 contract 的模型静默 resume。

产品最终模型使用 `models/v3/policy.pt`，不依赖本目录中的训练资产。
