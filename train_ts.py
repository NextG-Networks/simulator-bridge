from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "ns-o-ran-gym" / "src"))

from stable_baselines3 import PPO
from stable_baselines3.common.monitor import Monitor
from environments.ts_env import TrafficSteeringEnv


NS3_PATH = ROOT / "ns-3-mmwave-oran"

SCEN = ROOT / "ns-o-ran-gym" / "src" / "environments" / "scenario_configurations" / "ts_use_case.json"

OUT = ROOT / "out_ts"
OUT.mkdir(parents=True, exist_ok=True)

with open(SCEN, "r") as f:
    scen_cfg_raw = json.load(f)

env = TrafficSteeringEnv(
    ns3_path=str(NS3_PATH),
    scenario_configuration=scen_cfg_raw,
    output_folder=str(OUT),
    verbose=True,
    optimized=False   # or True, if you intend to run the optimized ns-3 path
)


print("obs_space:", env.observation_space, "action_space:", env.action_space)
obs, info = env.reset()
print("reset OK; obs type/shape:", type(obs), getattr(obs, "shape", None))

env = Monitor(env)

model = PPO("MlpPolicy", env, verbose=1, n_steps=1024, batch_size=64, gamma=0.99)
model.learn(total_timesteps=10_000)
model.save(str(OUT / "ppo_ts_policy"))
env.close()
