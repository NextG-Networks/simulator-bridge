
from __future__ import annotations
from os import path, stat
from os import path, stat
import csv
from typing import Dict, Iterable, List, Tuple, Union, Any

ActionPairs = List[Tuple[int, int]]               # e.g., [(ueId, cellId)]
ActionRows  = List[Dict[str, Any]]                # fully-formed rows for xApp bus
MultiHeader = Dict[str, Union[ActionPairs, ActionRows]]

class ActionController():
    """
    Backwards compatible controller that writes:
      1) Legacy HO file (ts_actions_for_ns3.csv)
      2) Unified xApp action bus (xapp_actions.csv) per the proposed schema
    """
    directory : str
    log_filename : str
    control_filename : str
    xapp_filename : str

    # Fixed header for the unified bus
    XAPP_HEADER = [
        "timestamp","type","scope","ueId","cellId","sliceId",
        "policy","policy_params",
        "dl_mcs_min","dl_mcs_max","ul_mcs_min","ul_mcs_max",
        "prb_weight","slice_weight","gbr_bps","mbr_bps",
        "granularity_period_ms","enable_ue_level",
    ]

    def __init__(self, sim_path, log_filename, control_filename, header):
        """Initialize Controller and its files
        Args:
            sim_path (str): the simulation path
            log_filename (str): purely for logging; not read by ns-3
            control_filename (str): legacy HO control file read by ns-3
            header (list[str]): columns for the legacy HO file
        """
        self.directory = sim_path
        self.log_filename = log_filename
        self.control_filename = control_filename
        self.xapp_filename = "xapp_actions.csv"

        # Init legacy log file with header
        with open(path.join(self.directory, self.log_filename), 'w') as file:
            file.write(f"{','.join(header)}\n")
            file.flush()

        # Ensure legacy control file exists (append mode, no header by design)
        open(path.join(self.directory, self.control_filename), 'a').close()

        # Ensure xapp_actions.csv exists with header
        self._ensure_file_with_header(path.join(self.directory, self.xapp_filename), self.XAPP_HEADER)

    @staticmethod
    def _ensure_file_with_header(filepath, header_cols):
        need_header = True
        try:
            st = stat(filepath)
            need_header = (st.st_size == 0)
        except FileNotFoundError:
            # file will be created
            pass
        with open(filepath, 'a') as f:
            if need_header:
                f.write(",".join(header_cols) + "\n")
                f.flush()

    def _write_legacy_row(self, timestamp: int, ueId: int, targetCell: int):
        legacy_line = f"{timestamp},{ueId},{targetCell}\n"
        with open(path.join(self.directory, self.control_filename), 'a') as fctl,              open(path.join(self.directory, self.log_filename), 'a') as flog:
            fctl.write(legacy_line); fctl.flush()
            flog.write(legacy_line); flog.flush()

    # def _write_xapp_ho_row(self, timestamp: int, ueId: int, targetCell: int):
    #     """Write a HANDOVER row to xapp_actions.csv using the unified schema."""
    #     # Columns:
    #     # timestamp,type,scope,ueId,cellId,sliceId,policy,policy_params,
    #     # dl_mcs_min,dl_mcs_max,ul_mcs_min,ul_mcs_max,prb_weight,slice_weight,
    #     # gbr_bps,mbr_bps,granularity_period_ms,enable_ue_level
    #     fields = [
    #         str(timestamp), "HANDOVER", "UE",
    #         str(ueId), str(targetCell), "",  # ueId, cellId, sliceId
    #         "", "",                          # policy, policy_params
    #         "", "", "", "",                  # dl/ul mcs bounds
    #         "", "", "", "",                  # prb/slice/GBR/MBR
    #         "", ""                           # reporting
    #     ]
    #     line = ",".join(fields) + "\n"
    #     with open(path.join(self.directory, self.xapp_filename), 'a') as fx:
    #         fx.write(line); fx.flush()
    def _write_xapp_rows(self, rows: ActionRows) -> None:
        """
        Write arbitrary rows to xapp_actions.csv.
        Unknown/missing columns are handled safely:
          - Only known columns in XAPP_HEADER are written
          - Missing values are written as blanks
        """
        filepath = path.join(self.directory, self.xapp_filename)
        with open(filepath, 'a', newline='') as fx:
            writer = csv.DictWriter(fx, fieldnames=self.XAPP_HEADER, extrasaction='ignore')
            for row in rows:
                # ensure only str/int/float/None types; coerce others to str
                safe_row = {k: ("" if v is None else v) for k, v in row.items()}
                writer.writerow(safe_row)
            fx.flush()

    # def create_control_action(self, timestamp: int, actions):
    #     """Applies the control action by writing it in the appropriate files
    #         timestamp (int) : action's timestamp
    #         actions [(tuple)]: list of tuples (ueId, targetCellId)
    #     """
    #     for action in actions:
    #         ueId, targetCell = map(int, action)
    #         # 1) Legacy path for current scenario
    #         self._write_legacy_row(timestamp, ueId, targetCell)
    #         # 2) Unified xapp action bus (HANDOVER)
    #         self._write_xapp_ho_row(timestamp, ueId, targetCell)
    def create_control_action(
        self,
        timestamp: int,
        actions: Union[ActionPairs, MultiHeader]
    ) -> None:
        """
        Apply control action(s):

        - Legacy input: [(ueId, targetCellId), ...] → writes legacy + HANDOVER xApp rows
        - Multi-header input:
            {
              "HANDOVER": [(ueId, cellId), ...],               # pairs mode
              "TX_POWER": [{"scope":"UE","ueId":1,"policy":"P"}],  # full-row mode
              ...
            }
          → writes xApp rows for each header. Still writes legacy for HANDOVER pairs.
        """
        if isinstance(actions, list):
            # Legacy single-header behavior (HANDOVER pairs)
            for ueId, targetCell in actions:
                ueId, targetCell = int(ueId), int(targetCell)
                self._write_legacy_row(timestamp, ueId, targetCell)
            # Mirror into xApp bus as HANDOVER
            rows = [self._row_from_pair(timestamp, "HANDOVER", "UE", ueId, targetCell)
                    for (ueId, targetCell) in actions]
            self._write_xapp_rows(rows)
            return

        # Multi-header dict
        all_rows: ActionRows = []
        for header, payload in actions.items():
            h = str(header).upper()

            if isinstance(payload, list) and payload and isinstance(payload[0], tuple):
                # pairs mode → assume scope UE and map (ueId, cellId)
                pairs: ActionPairs = [(int(u), int(c)) for (u, c) in payload]  # type: ignore
                # legacy mirror only for HANDOVER
                if h == "HANDOVER":
                    for ueId, cellId in pairs:
                        self._write_legacy_row(timestamp, ueId, cellId)
                all_rows.extend(
                    [self._row_from_pair(timestamp, h, "UE", ueId, cellId) for (ueId, cellId) in pairs]
                )
            else:
                # full-row mode → caller provides dicts; we overlay defaults
                rows: ActionRows = []
                for item in payload:  # type: ignore
                    if not isinstance(item, dict):
                        continue
                    rows.append(self._row_with_defaults(timestamp, h, item))
                all_rows.extend(rows)

        if all_rows:
            self._write_xapp_rows(all_rows)
# ---------------- Helpers ---------------- #
    def _row_from_pair(self, ts: int, header: str, scope: str, ueId: int, cellId: int) -> Dict[str, Any]:
        """Minimal row from a (ueId, cellId) pair."""
        return {
            "timestamp": ts,
            "type": header,
            "scope": scope,      # usually "UE" for pairs
            "ueId": ueId,
            "cellId": cellId,
            # fill rest as blanks by default
        }

    def _row_with_defaults(self, ts: int, header: str, row: Dict[str, Any]) -> Dict[str, Any]:
        """Overlay required fields + defaults while preserving any provided extras."""
        base = {
            "timestamp": ts,
            "type": header,
            "scope": row.get("scope", "UE"),
            "ueId": row.get("ueId", ""),
            "cellId": row.get("cellId", ""),
            "sliceId": row.get("sliceId", ""),
            "policy": row.get("policy", ""),
            "policy_params": row.get("policy_params", ""),
            "dl_mcs_min": row.get("dl_mcs_min", ""),
            "dl_mcs_max": row.get("dl_mcs_max", ""),
            "ul_mcs_min": row.get("ul_mcs_min", ""),
            "ul_mcs_max": row.get("ul_mcs_max", ""),
            "prb_weight": row.get("prb_weight", ""),
            "slice_weight": row.get("slice_weight", ""),
            "gbr_bps": row.get("gbr_bps", ""),
            "mbr_bps": row.get("mbr_bps", ""),
            "granularity_period_ms": row.get("granularity_period_ms", ""),
            "enable_ue_level": row.get("enable_ue_level", "")
        }
        # Any extra keys are kept but ignored by DictWriter (extrasaction='ignore')
        base.update(row)
        return base