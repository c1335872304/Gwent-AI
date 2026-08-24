"""兼容入口。

正式训练入口已经迁移到 :mod:`gwent_rl.train_ppo`。
保留本模块是为了不破坏旧脚本、历史命令和已有 smoke test。
"""

from .train_ppo import main, train

__all__ = ["main", "train"]


if __name__ == "__main__":
    main()
