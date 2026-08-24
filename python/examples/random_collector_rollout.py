from gwent_rl import RlCollector, random_legal_actions

with RlCollector(num_envs=128, max_batch_size=128, base_seed=0) as collector:
    for _ in range(32):
        batch = collector.collect()
        actions = random_legal_actions(batch)
        collector.apply_actions(batch, actions)
    print("steps", collector.total_steps, "completed", collector.completed_episodes)
