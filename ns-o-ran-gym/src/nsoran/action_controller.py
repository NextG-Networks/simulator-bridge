
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

    # when preparing paths:

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
        self.xapp_filename = "/home/hybrid/proj/out_ts/ts_actions_for_ns3.csv"

        # Init legacy log file with header
        # with open(path.join(self.directory, self.log_filename), 'w') as file:
        #     file.write(f"{','.join(header)}\n")
        #     file.flush()

        # Ensure legacy control file exists (append mode, no header by design)
        # open(path.join(self.directory, self.control_filename), 'a').close()

        # Ensure xapp_actions.csv exists with header
        # self._ensure_file_with_header(path.join(self.directory, self.xapp_filename), self.XAPP_HEADER)

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

    def _write_xapp_ho_row(self, timestamp: int, ueId: int, targetCell: int):
        """Write a HANDOVER row to xapp_actions.csv using the unified schema."""
        # Columns:
        # timestamp,type,scope,ueId,cellId,sliceId,policy,policy_params,
        # dl_mcs_min,dl_mcs_max,ul_mcs_min,ul_mcs_max,prb_weight,slice_weight,
        # gbr_bps,mbr_bps,granularity_period_ms,enable_ue_level
        fields = [
            str(timestamp), "HANDOVER", "UE",
            str(ueId), str(targetCell), "0",  # ueId, cellId, sliceId
            "0", "0",                          # policy, policy_params
            "0", "0", "0", "0",                  # dl/ul mcs bounds
            "0", "0", "0", "0",                  # prb/slice/GBR/MBR
            "0", "0"                           # reporting
        ]
        line = ",".join(fields) + "\n"
        with open(path.join(self.directory, self.xapp_filename), 'a') as fx:
            fx.write(line); fx.flush()
    # def _write_xapp_rows(self, rows: ActionRows) -> None:
    #     """
    #     Write arbitrary rows to xapp_actions.csv.
    #     Unknown/missing columns are handled safely:
    #       - Only known columns in XAPP_HEADER are written
    #       - Missing values are written as blanks
    #     """
    #     filepath = path.join(self.directory, self.xapp_filename)
    #     with open(filepath, 'a', newline='') as fx:
    #         writer = csv.DictWriter(fx, fieldnames=self.XAPP_HEADER, extrasaction='ignore')
    #         for row in rows:
    #             # ensure only str/int/float/None types; coerce others to str
    #             safe_row = {k: ("" if v is None else v) for k, v in row.items()}
    #             writer.writerow(safe_row)
    #         fx.flush()

    def create_control_action(self, timestamp: int, actions):
        """Applies the control action by writing it in the appropriate files
            timestamp (int) : action's timestamp
            actions [(tuple)]: list of tuples (ueId, targetCellId)
        """
        print(f"Writing actions at timestamp {timestamp}: {actions}")
        for action in actions:
            ueId, targetCell = map(int, action)
            #self._write_legacy_row(timestamp, ueId, targetCell)
            self._write_xapp_ho_row(timestamp, ueId, targetCell)
    