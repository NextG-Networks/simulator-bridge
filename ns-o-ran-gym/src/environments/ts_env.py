from nsoran.ns_env import NsOranEnv
from gymnasium import spaces
import logging
import numpy as np
import os
from pathlib import Path

class TrafficSteeringEnv(NsOranEnv):
    def __init__(self, ns3_path: str, scenario_configuration: dict, output_folder: str,
                 optimized: bool, verbose=False, time_factor=0.001, Cf=1.0, lambdaf=0.1,
                 control_file: str = None):
        """Environment specific parameters:
            verbose (bool): enables logging
            time_factor (float): applies conversion from seconds to another multiple (eg. ms). See compute_reward
            Cf (float): Cost factor for handovers. See compute_reward
            lambdaf (float): Decay factor for handover cost. See compute_reward
        """
        print("Initializing TrafficSteeringEnv...")
        cf = control_file or scenario_configuration.get("controlFileName", "xapp_actions.csv")
        if isinstance(cf, list):
            cf = cf[0]
        cf_abs = str(Path(cf).resolve())
        print( "Using control file at:", cf_abs)
        super().__init__(
            ns3_path=ns3_path,
            scenario='scenario-one',
            scenario_configuration=scenario_configuration,
            output_folder=output_folder,
            optimized=optimized,
            control_header=["timestamp","type","scope","ueId","cellId","sliceId",
            "policy","policy_params",
            "dl_mcs_min","dl_mcs_max","ul_mcs_min","ul_mcs_max",
            "prb_weight","slice_weight","gbr_bps","mbr_bps",
            "granularity_period_ms","enable_ue_level"],
            log_file='TsActions.txt',
            control_file=cf_abs
        )

        print("Init Done")

        # === Topology params ===
        # Default to 7 (original paper setup), but allow overriding from scenario JSON.
        self.n_gnbs = int(self.scenario_configuration.get('n_gnbs', 7))
        self.n_ues = int(self.scenario_configuration.get('ues', 1))

        # === Features used for state ===
        self.columns_state = [
            'RRU.PrbUsedDl',
            'L3 serving SINR',
            'DRB.MeanActiveUeDl',
            'TB.TotNbrDlInitial.Qpsk',
            'TB.TotNbrDlInitial.16Qam',
            'TB.TotNbrDlInitial.64Qam',
            'TB.TotNbrDlInitial'
        ]
        # +1 column for timestamp
        self._feat_dim = len(self.columns_state) + 1

        # === Rewards need these ===
        self.columns_reward = ['DRB.UEThpDl.UEID', 'nrCellId']

        # === Observation/Action spaces ===
        # State is [UE x Cell, features(+timestamp)]
        self.observation_space = spaces.Box(
            shape=(self.n_ues * self.n_gnbs, self._feat_dim),
            low=-np.inf, high=np.inf, dtype=np.float64
        )

        # Actions: one choice per (UE x Cell) “slot”.
        # If there is only ONE cell, restrict to "no-op" (dimension=1) to avoid illegal HOs.
        if self.n_gnbs <= 1:
            self.action_space = spaces.MultiDiscrete([1] * (self.n_ues * self.n_gnbs))
            self._action_targets = []  # no valid target cells
        else:
            # Keep the original logic: index 0 = no-op (we skip), 1..(n_gnbs-1) map to cell IDs.
            # Map indices to synthetic cell IDs (2..(1+n_gnbs)) unless you have real IDs to inject.
            self.action_space = spaces.MultiDiscrete([self.n_gnbs] * (self.n_ues * self.n_gnbs))
            self._action_targets = list(range(2, 2 + self.n_gnbs))  # e.g., [2,3,4,5,6,7,8]

        # Cumulative state for reward
        self.previous_df = None
        self.previous_kpms = None
        self.handovers_dict = dict()

        # Logging / reward params
        self.verbose = verbose
        if self.verbose:
            logging.basicConfig(filename='./reward_ts.log', level=logging.DEBUG,
                                format='%(asctime)s - %(message)s')
        self.time_factor = time_factor
        self.Cf = Cf
        self.lambdaf = lambdaf
        

    # -------------------------
    # Helpers
    # -------------------------
    def _safe_obs(self, arr: np.ndarray) -> np.ndarray:
        """Force obs to the declared shape and remove NaN/Inf."""
        exp = self.observation_space.shape
        arr = np.array(arr, dtype=np.float64, copy=False)

        if arr.ndim == 0:
            arr = np.zeros(exp, dtype=np.float64)
        elif arr.shape != exp:
            out = np.zeros(exp, dtype=np.float64)
            r = min(arr.shape[0] if arr.ndim >= 1 else 0, exp[0])
            c = min(arr.shape[1] if arr.ndim >= 2 else 0, exp[1])
            if r > 0 and c > 0:
                out[:r, :c] = arr[:r, :c]
            arr = out

        return np.nan_to_num(arr, nan=0.0, posinf=0.0, neginf=0.0)

    # -------------------------
    # Env-required impls
    # -------------------------
    def _compute_action(self, action) -> list[tuple]:
        """
        Convert MultiDiscrete vector into list of (ueId, targetCellId) commands for ns-3.
        Index 0 in each dimension is treated as 'no-op'.
        """
        print("Computing action...")
        action_list = []
        if self.n_gnbs <= 1:
            # Single-cell case: no valid handovers
            return action_list

        a = np.array(action, dtype=np.int64).reshape(-1)
        # Map indices 1..(n_gnbs-1) to cell IDs via self._action_targets
        print("Action array:", a)
        for ue_slot, idx in enumerate(a):
            if idx <= 0:
                continue  # no-op
            idx = int(min(idx, self.n_gnbs - 1))
            target_cell = int(self._action_targets[idx])
            # ueId for control plane is 1-based
            action_list.append((ue_slot + 1, target_cell))

        if self.verbose:
            logging.debug(f'Action list {action_list}')
        print("Action list:", action_list)
        return action_list

    def _get_obs(self) -> np.ndarray:
        """
        Build observation matrix of shape (n_ues * n_gnbs, feat_dim).
        We read per-UE KPIs; if missing/short, we pad with zeros and append timestamp column.
        """
        ue_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_state)

        # Turn list of per-UE KPI vectors into a 2D float array
        X = np.array(ue_kpms, dtype=object)
        try:
            rows = [np.array(row, dtype=np.float64, copy=False).reshape(1, -1) for row in X]
            X = np.vstack(rows) if rows else np.zeros((0, len(self.columns_state)), dtype=np.float64)
        except Exception:
            X = np.zeros((0, len(self.columns_state)), dtype=np.float64)

        # Add timestamp as the last column
        if X.shape[0] == 0:
            obs_raw = np.zeros((0, self._feat_dim), dtype=np.float64)
        else:
            ts_col = np.full((X.shape[0], 1), float(self.last_timestamp), dtype=np.float64)
            obs_raw = np.hstack([X, ts_col])

        # Force declared shape and clean NaNs/Inf
        return self._safe_obs(obs_raw)

    def _compute_reward(self) -> float:
        """
        Reward = sum_UE [ log10(throughput_new) - log10(throughput_old) - HO_cost ]
        HO_cost decays with time since last HO. NaNs are neutralized.
        """
        total_reward = 0.0
        current_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_reward)

        if self.previous_kpms is None:
            if self.verbose:
                logging.debug(f'Starting first reward computation at timestamp {self.last_timestamp}')
            self.previous_timestamp = self.last_timestamp - (self.scenario_configuration['indicationPeriodicity'] * 1000)
            self.previous_kpms = self.datalake.read_kpms(self.previous_timestamp, self.columns_reward)
        print("Setting up reward computation...")
        print("Previous KPIs:", self.previous_kpms)
        print("Current KPIs:", current_kpms)
        for t_o, t_n in zip(self.previous_kpms, current_kpms):
            ueImsi_o, ueThpDl_o, sourceCell = t_o
            ueImsi_n, ueThpDl_n, currentCell = t_n
            if ueImsi_n != ueImsi_o:
                if self.verbose:
                    logging.error(f"UE IMSI mismatch: {ueImsi_o} != {ueImsi_n} (ts: {self.last_timestamp})")
                continue

            # HO cost
            HoCost = 0.0
            if currentCell != sourceCell:
                lastHo = self.handovers_dict.get(ueImsi_n, 0)
                if lastHo != 0:
                    timeDiff = (self.last_timestamp - lastHo) * self.time_factor
                    HoCost = self.Cf * ((1 - self.lambdaf) ** timeDiff)
                self.handovers_dict[ueImsi_n] = self.last_timestamp

            # Log throughput (safe)
            LogOld = np.log10(ueThpDl_o) if ueThpDl_o not in (0, None) else 0.0
            LogNew = np.log10(ueThpDl_n) if ueThpDl_n not in (0, None) else 0.0

            reward_ue = float(np.nan_to_num(LogNew - LogOld - HoCost, nan=0.0))
            if self.verbose:
                logging.debug(f"Reward UE {ueImsi_n}: {reward_ue} (Δlog={LogNew-LogOld}, HoCost={HoCost})")
            total_reward += reward_ue

        if self.verbose:
            logging.debug(f"Total reward: {total_reward}")
        self.previous_kpms = current_kpms
        self.previous_timestamp = self.last_timestamp
        self.reward = float(np.nan_to_num(total_reward, nan=0.0))
        return self.reward




# ==== ORIGINAL ====
# from nsoran.ns_env import NsOranEnv 
# from gymnasium import spaces
# import logging

# import numpy as np

# class TrafficSteeringEnv(NsOranEnv):
#     def __init__(self, ns3_path:str, scenario_configuration:dict, output_folder:str, optimized:bool, verbose=False, time_factor=0.001, Cf=1.0, lambdaf=0.1):
#         """Environment specific parameters:
#             verbose (bool): enables logging
#             time_factor (float): applies convertion from seconds to another multiple (eg. ms). See compute_reward
#             Cf (float): Cost factor for handovers. See compute_reward
#             lambdaf (float): Decay factor for handover cost. See compute_reward
#         """
#         super().__init__(ns3_path=ns3_path, scenario='scenario-one', scenario_configuration=scenario_configuration,
#                          output_folder=output_folder, optimized=optimized,
#                          control_header = ['timestamp','ueId','nrCellId'], log_file='TsActions.txt', control_file='ts_actions_for_ns3.csv')
#         # These features can be hardcoded since they are specific for the use case
#         self.columns_state = ['RRU.PrbUsedDl', 'L3 serving SINR', 'DRB.MeanActiveUeDl', 
#                               'TB.TotNbrDlInitial.Qpsk', 'TB.TotNbrDlInitial.16Qam', 
#                               'TB.TotNbrDlInitial.64Qam', 'TB.TotNbrDlInitial']

#         # We need the throughput as well as the cell id to determine whether an handover occurred
#         self.columns_reward = ['DRB.UEThpDl.UEID', 'nrCellId']
#         # In the traffic steering use case, the action is a combination between 
#         n_gnbs = 7  # scenario one has always 7 gnbs 
#         n_actions_ue = 7 # each UE can connect to a gNB identified by ID (from 2 to 8), 0 is No Action
#         # obs_space size: (# ues_per_gnb * # n_gnbs, # observation_columns + timestamp = 1)
#         self.observation_space = spaces.Box(shape=(self.scenario_configuration['ues']*n_gnbs,len(self.columns_state)+1), low=-np.inf, high=np.inf, dtype=np.float64)
#         self.action_space = spaces.MultiDiscrete([n_actions_ue] * self.scenario_configuration['ues'] *  n_gnbs)
#         # Stores the kpms of the previous timestamp (see compute_reward)
#         self.previous_df = None
#         self.previous_kpms = None
#         # Auxiliary functions to keep track of last handover time (see compute_reward)
#         self.handovers_dict = dict()
#         self.verbose = verbose
#         if self.verbose:
#             logging.basicConfig(filename='./reward_ts.log', level=logging.DEBUG, format='%(asctime)s - %(message)s')
#         self.time_factor = time_factor
#         self.Cf = Cf
#         self.lambdaf = lambdaf

#     def _compute_action(self, action) -> list[tuple]:    
#         # action from multidiscrete shall become a list of ueId, targetCell.
#         # If a targetCell is 0, it means No Handover, thus we don't send it
#         action_list = []
#         for ueId, targetCellId in enumerate(action):
#             if targetCellId != 0: # and 
#                 # Once we are in this condition, we need to transform the action from the one of gym to the one of ns-O-RAN
#                 action_list.append((ueId + 1, targetCellId + 2))
#         if self.verbose:
#             logging.debug(f'Action list {action_list}')
#         return action_list

#     def _fill_datalake_usecase(self):
#         # We don't need fill_datalake_usecase in TS use case
#         pass

#     def _get_obs(self) -> list:
#         ue_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_state)                          
#         # 'TB.TOTNBRDLINITIAL.QPSK_RATIO', 'TB.TOTNBRDLINITIAL.16QAM_RATIO', 'TB.TOTNBRDLINITIAL.64QAM_RATIO'
#         # From per-UE values we need to extract per-Cell Values
#         # obs_kpms = []
#         # for ue_kpm in ue_kpms:
#         #     imsi, kpms = ue_kpm
#         #     obs_kpms.append(kpms)

#         # _RATIO values are the per Cell value / Tot nbr dl initial

#         self.observations = np.array(ue_kpms)
#         return self.observations
    
#     def _compute_reward(self) -> float:
#         # Computes the reward for the traffic steering environment. Based off journal on TS
#         # The total reward is the sum of per ue rewards, calculated as the difference in the
#         # logarithmic throughput between indication periodicities. If an UE experienced an HO,
#         # its reward takes into account a cost function related to said handover. The cost
#         # function punishes frequent handovers.
#         # See the docs for more info.

#         total_reward = 0.0
#         current_kpms = self.datalake.read_kpms(self.last_timestamp, self.columns_reward)

#         # If this is the first iteration we do not have the previous kpms
#         if self.previous_kpms is None:
#             if self.verbose:
#                 logging.debug(f'Starting first reward computation at timestamp {self.last_timestamp}')
#             self.previous_timestamp = self.last_timestamp - (self.scenario_configuration['indicationPeriodicity'] * 1000)
#             self.previous_kpms = self.datalake.read_kpms(self.previous_timestamp, self.columns_reward)

#         #Assuming they are of the same lenght
#         for t_o, t_n in zip(self.previous_kpms, current_kpms):
#             ueImsi_o, ueThpDl_o, sourceCell = t_o
#             ueImsi_n, ueThpDl_n, currentCell = t_n
#             if ueImsi_n == ueImsi_o:
#                 HoCost = 0
#                 if currentCell != sourceCell:
#                     lastHo = self.handovers_dict.get(ueImsi_n, 0)  # Retrieve last handover time or default to 0
#                     if lastHo != 0: # If this is the first HO the cost is 0
#                         timeDiff = (self.last_timestamp - lastHo) * self.time_factor
#                         HoCost = self.Cf * ((1 - self.lambdaf) ** timeDiff)
#                     self.handovers_dict[ueImsi_n] = self.last_timestamp  # Update dictionary
 
#                 LogOld = 0
#                 LogNew = 0
#                 if ueThpDl_o != 0:
#                     LogOld = np.log10(ueThpDl_o)
#                 if ueThpDl_n != 0:
#                     LogNew = np.log10(ueThpDl_n)

#                 LogDiff = LogNew - LogOld
#                 reward_ue = LogDiff - HoCost
#                 if self.verbose:
#                     logging.debug(f"Reward for UE {ueImsi_n}: {reward_ue} (LogDiff: {LogDiff}, HoCost: {HoCost})")
#                 total_reward += reward_ue
#             else:
#                 if self.verbose:
#                     logging.error(f"Unexpected UeImsi mismatch: {ueImsi_o} != {ueImsi_n} (current ts: {self.last_timestamp})")
#         if(self.verbose):
#             logging.debug(f"Total reward: {total_reward}")
#         self.previous_kpms = current_kpms
#         self.previous_timestamp = self.last_timestamp
#         self.reward = total_reward
#         return self.reward
