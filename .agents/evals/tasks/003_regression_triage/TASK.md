# Eval 003：训练回归排查

不要先改参数。先提交排查方案。

现象：

> 最近一次改动后 PPO loss 仍是有限值，训练也能保存 checkpoint，但 matchup 胜率明显下降；同时没有 CTest 失败。

已知最近改动涉及 collector 和 reward timing。

## 期望观察点

优秀方案应该优先检查：

1. observation/action mapping 是否仍正确；
2. reward attribution 的 actor/side/sign/timing；
3. terminal trajectory 与 done 边界；
4. GAE/return；
5. evaluation protocol 是否变化；
6. 最后才是调 learning rate 等超参数。

应该建议构造小规模确定性复现，而不是直接再跑长训练。
