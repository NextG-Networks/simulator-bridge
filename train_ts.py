from pathlib import Path
import json
import numpy as np
import os, subprocess,sys
import time

# os.environ["NS_LOG"] = "prefix_time|prefix_func|LteEnbRrc=level_info|LteEnbNetDevice=level_info"
# # ✅ Use only components from your print-list; ':' separates components, '|' separates flags
os.environ["NS_LOG"] = (
    "ScenarioOne=level_info:"  # your program component
    "LteEnbRrc=level_info:" 
    "LteUeRrc=level_info:"
    "LteEnbNetDevice=level_info:"
    "LteUeNetDevice=level_info:"
    "MmWaveEnbNetDevice=level_info:"
    "MmWaveUeNetDevice=level_info"
)
# os.environ.pop("NS_LOG", None)  # clear previous settings
# os.environ["NS_LOG"] = "ScenarioOne=level_info"



from stable_baselines3 import PPO
from stable_baselines3.common.monitor import Monitor
from environments.ts_env import TrafficSteeringEnv

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "ns-o-ran-gym" / "src"))
NS3_PATH = ROOT / "ns-3-mmwave-oran"
SCEN = ROOT / "ns-o-ran-gym" / "src" / "environments" / "scenario_configurations" / "ts_use_case.json"


OUT = ROOT / "out_ts"
OUT.mkdir(parents=True, exist_ok=True)

CONTROL_ABS = str((OUT / "ts_actions_for_ns3.csv").resolve())
Path(CONTROL_ABS).touch(exist_ok=True)
Path(CONTROL_ABS).parent.mkdir(parents=True, exist_ok=True)


# # LOG for NS-3
# cmd = ["./ns3", "run", f"scratch/scenario-one --controlFileName={CONTROL_ABS}"]
# print("=== before proc ===")


# proc = subprocess.Popen(
#     cmd,
#     cwd="ns-3-mmwave-oran",  # run inside your NS-3 repo
#     env=os.environ,
#     stdout=subprocess.PIPE,
#     stderr=subprocess.PIPE,
#     text=True
# )

# print("=== after proc ===")

# out, err = proc.communicate()

# print("=== NS3 OUTPUT ===")
# print(out)
# print("=== NS3 ERRORS ===")
# print(err)


print("CONTROL_ABS:", CONTROL_ABS)
def wait_for_kpms(env, cols, min_rows=1, timeout_s=5.0, poll_s=0.05):
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        rows = env.datalake.read_kpms(env.last_timestamp, cols) or []
        if len(rows) >= min_rows:
            return rows
        time.sleep(poll_s)
    return []

with open(SCEN, "r") as f:
    scen_cfg_raw = json.load(f)
 
# keep everything as lists because ns_env does v[0]
def as_list(x): 
    return x if isinstance(x, list) else [x]

scen_cfg_raw.update({
    "controlFileName": as_list(CONTROL_ABS),
    "useSemaphores":   as_list(1),
    "indicationPeriodicity": as_list(0.1),
    # optional while debugging:
    "simTime": as_list(3.0),
})

print("scen_cfg_raw:",scen_cfg_raw,"\n\n")

env = TrafficSteeringEnv(
    ns3_path=str(NS3_PATH),
    scenario_configuration=scen_cfg_raw,
    output_folder=str(OUT),
    verbose=True,
    optimized=False
)
print("env.optimized:", env.optimized)


# env.control_file = CONTROL_ABS  # override if needed
# env = TrafficSteeringEnv()
print("obs_space:", env.observation_space, "action_space:", env.action_space)
obs, info = env.reset()

cols = ['DRB.UEThpDl.UEID', 'nrCellId']
baseline = wait_for_kpms(env, cols, min_rows=1, timeout_s=3.0)
if not baseline:
    raise RuntimeError("No KPIs produced after reset; increase simTime or check NS-3 run.")

# Seed the reward state so _compute_reward() won’t zip None
env.previous_kpms = baseline
env.previous_timestamp = env.last_timestamp

#baseline = env.datalake.read_kpms(env.last_timestamp, ['DRB.UEThpDl.UEID', 'nrCellId'])
# forced = np.ones(env.action_space.shape, dtype=int)

# after = wait_for_kpms(env, cols, min_rows=1, timeout_s=3.0)

#after = env.datalake.read_kpms(env.last_timestamp, ['DRB.UEThpDl.UEID', 'nrCellId'])

# print("Baseline KPIs:", baseline[:3])
# print("After-step KPIs:", after[:3])

# obs, reward, terminated, truncated, info = env.step(forced)

#Flush to disk immediately (the env writes control CSV under env.sim_path)
#env.datalake.flush() if hasattr(env, "datalake") else None



print("SIM_PATH", env.sim_path)
print("xapp_actions target:", os.path.join(env.sim_path, os.path.basename(CONTROL_ABS)))
print("reset OK; obs type/shape:", type(obs), getattr(obs, "shape", None))

env = Monitor(env)
# BLACKBOX
model = PPO("MlpPolicy", env, verbose=1, n_steps=1024, batch_size=64, gamma=0.99)
model.learn(total_timesteps=10_000)
print("Learning OK.")
model.save(str(OUT / "ppo_ts_policy"))
print("Learning complete.")
env.close()
print("Environment closed.")