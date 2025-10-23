import numpy as np
import logging
from gymnasium import spaces
from nsoran.ns_env import NsOranEnv

# Define a name for our new log component
logger = logging.getLogger(__name__)

class RadioResourceEnv(NsOranEnv):
    """
    A simple environment for a 1-gNB, 1-UE mmWave scenario.
    The agent learns to control Downlink MCS, Uplink MCS, and TX Power.
    """
    def __init__(self, ns3_path: str, scenario_configuration: dict, output_folder: str,
                 optimized: bool, verbose=False, power_cost_factor=0.1):
        """
        power_cost_factor (float): Penalty multiplier for using higher power levels.
        """
        
        # === 1. DEFINE THE CONTROL INTERFACE ===
        # This tells the base class what to write to the ns-3 control file.
        # Your C++ scenario MUST be modified to read these values.
        super().__init__(
            ns3_path=ns3_path,
            # scenario='MVS_Mmwave_1gNB_1UE',  # Matches your C++ file's NS_LOG_COMPONENT_DEFINE
            scenario='scenario-hybrid-withstuff',
            scenario_configuration=scenario_configuration,
            output_folder=output_folder,
            optimized=optimized,
            # NEW: Define the columns for the control file
            control_header=['timestamp', 'ueImsi', 'mcsDl', 'mcsUl', 'txPower'],
            log_file='RrActions.txt',
            control_file='rr_actions_for_ns3.csv' # New file for our new actions
        )

        # === 2. DEFINE TOPOLOGY PARAMS ===
        # This scenario is fixed, so we can hardcode these.
        self.n_gnbs = 1
        self.n_ues = 1
        
        # === 3. DEFINE STATE (OBSERVATION) FEATURES ===
        # These are the KPIs we will read from ns-3 to build our observation.
        self.columns_state = [
            'L3 serving SINR',    # Main driver for MCS
            'DRB.UEThpDl.UEID',   # DL Throughput
            'DRB.UEThpUl.UEID',   # UL Throughput
            'RRU.PrbUsedDl',      # Downlink PRB Usage
            'PHR'                 # Power Headroom Report (very useful for power control)
        ]
        # +1 column for the timestamp
        self._feat_dim = len(self.columns_state) + 1

        # === 4. DEFINE REWARD FEATURES ===
        # These are the KPIs needed to calculate the reward.
        # We only need throughput for our reward logic.
        self.columns_reward = ['DRB.UEThpDl.UEID']

        # === 5. DEFINE OBSERVATION SPACE ===
        # The observation is a single vector of features for our 1 UE.
        # Shape is (n_ues, n_features_ +_timestamp)
        self.observation_space = spaces.Box(
            shape=(self.n_ues, self._feat_dim),
            low=-np.inf, high=np.inf, dtype=np.float64
        )

        # === 6. DEFINE ACTION SPACE ===
        # The agent outputs 3 discrete values:
        # 1. DL MCS Index (0-29, ~30 levels)
        # 2. UL MCS Index (0-29, ~30 levels)
        # 3. TX Power Level (0-9, 10 abstract levels)
        self.action_space = spaces.MultiDiscrete([30, 30, 10])

        # === 7. REWARD & LOGGING PARAMS ===
        self.power_cost_factor = power_cost_factor
        self.last_action_taken = None  # Store the action for reward calculation
        self.verbose = verbose
        if self.verbose:
            # Configure logging for this specific class
            logging.basicConfig(level=logging.DEBUG,
                                format='%(asctime)s - %(name)s - %(message)s',
                                filename='./reward_rr.log')
            logger.info("RadioResourceEnv initialized")

    # -------------------------
    # Helper to clean observations
    # -------------------------
    def _safe_obs(self, arr: np.ndarray) -> np.ndarray:
        """Force obs to the declared shape and remove NaN/Inf."""
        exp = self.observation_space.shape
        arr = np.array(arr, dtype=np.float64, copy=False)

        # Handle empty or malformed data from ns-3
        if arr.ndim == 0 or arr.size == 0:
            arr = np.zeros(exp, dtype=np.float64)
        elif arr.shape != exp:
            out = np.zeros(exp, dtype=np.float64)
            # Try to fit the data if it's just a simple vector
            try:
                flat_arr = arr.flatten()
                fit_len = min(flat_arr.size, out.size)
                out.flat[:fit_len] = flat_arr[:fit_len]
            except Exception:
                pass # Fallback to zeros
            arr = out

        return np.nan_to_num(arr, nan=0.0, posinf=0.0, neginf=0.0)

    # -------------------------
    # Env-required implementations
    # -------------------------
    
    def _get_obs(self) -> np.ndarray:
        """
        Build observation matrix. For 1 UE, this will be (1, n_features + 1).
        """
        # Read the list of KPIs for all UEs (in our case, just one)
        ue_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_state)
        
        # Convert list of lists to a 2D numpy array
        try:
            X = np.array(ue_kpms, dtype=np.float64)
            if X.ndim == 1: # Ensure it's 2D
                X = X.reshape(1, -1)
        except Exception as e:
            logger.warning(f"Could not form KPI array: {e}. Returning zeros.")
            X = np.zeros((self.n_ues, len(self.columns_state)), dtype=np.float64)

        # Add timestamp as the last column
        ts_col = np.full((X.shape[0], 1), float(self.last_timestamp), dtype=np.float64)
        obs_raw = np.hstack([X, ts_col])

        # Force shape and clean NaNs
        return self._safe_obs(obs_raw)

    def _compute_action(self, action: np.ndarray) -> list[tuple]:
        """
        Convert the agent's action (e.g., [15, 12, 5]) into a control-file line.
        """
        # Store the action so the reward function can see it
        self.last_action_taken = action

        ue_imsi = 1  # In this scenario, we know the first UE's IMSI is 1
        mcs_dl = int(action[0])
        mcs_ul = int(action[1])
        tx_power = int(action[2]) # This is an "abstract" level (0-9)

        # The base class expects a list of tuples. Each tuple is one line
        # in the control file (minus the timestamp, which it adds).
        action_list = [(ue_imsi, mcs_dl, mcs_ul, tx_power)]
        
        if self.verbose:
            logger.debug(f"t={self.last_timestamp} | Action: {action} -> {action_list}")
            
        return action_list

    def _compute_reward(self) -> float:
        """
        Reward = log(Throughput) - (Power Cost)
        """
        # Read KPIs needed for reward. This returns a list: [(imsi, thpDl)]
        current_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_reward)

        if not current_kpms or not current_kpms[0]:
            logger.warning(f"t={self.last_timestamp} | No KPIs found for reward calculation.")
            return 0.0 # No data, no reward

        # --- Throughput Reward ---
        # We know we only have 1 UE, so we take the first item
        ue_imsi, thp_dl = current_kpms[0]
        
        # Use log of throughput to reward relative gains
        # (e.g., 1 to 10 Mbps is as good as 10 to 100 Mbps)
        log_thp = np.log10(thp_dl) if thp_dl > 0 else 0.0
        
        # --- Power Cost ---
        power_cost = 0.0
        if self.last_action_taken is not None:
            # Get the power level (0-9) from the last action
            power_level = self.last_action_taken[2]
            power_cost = self.power_cost_factor * power_level

        reward = float(np.nan_to_num(log_thp - power_cost, nan=0.0))

        if self.verbose:
            logger.debug(f"t={self.last_timestamp} | Reward: {reward} (logThp: {log_thp}, pCost: {power_cost})")
            
        return reward