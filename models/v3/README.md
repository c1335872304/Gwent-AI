# V3 Policy Slot

`models/v3/policy.pt` 是本地产品运行时使用的最终 V3 policy 位置。

一个 checkpoint 同时包含 Shared、Deck A Private 和 Deck B Private 参数。运行时由当前 deck id 选择对应私有分支。

该目录属于推理运行时资产位置；训练过程产生的中间 checkpoint 仍保留在各自的训练输出目录中。
