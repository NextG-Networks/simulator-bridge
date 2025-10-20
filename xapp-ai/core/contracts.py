from __future__ import annotations
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Optional, Dict, Any


@dataclass
class Header:
    ric_instance_id: str
    function_id: str
    kpm_version: str
    granularity_period_ms: int
    window_start: datetime
    window_end: datetime
    sequence_number: int


@dataclass
class CellMetrics:
    cell_id: str
    prb_used_dl: int
    prb_used_ul: int
    prb_total: int
    thr_dl_bps: float
    thr_ul_bps: float
    bler_dl: float
    bler_ul: float
    cqi_avg: float
    mcs_dl_avg: float
    mcs_ul_avg: float
    active_ue_count: int
    pdcp_pdu_tx: int
    pdcp_pdu_rx: int
    rlc_sdu_tx: int
    rlc_sdu_rx: int
    delay_p50_ms: float
    delay_p95_ms: float
    delay_p99_ms: float
    jitter_p95_ms: float
    handovers_triggered: int


@dataclass
class UEMetrics:
    ue_id: str
    cell_id: str
    rsrp_dbm: float
    rsrq_db: float
    sinr_db: float
    cqi_avg: float
    mcs_dl_avg: float
    mcs_ul_avg: float
    thr_dl_bps: float
    thr_ul_bps: float
    bler_dl: float
    bler_ul: float
    buffer_bytes_avg: float
    packet_delay_ms_p50: float
    packet_delay_ms_p95: float
    packet_delay_ms_p99: float
    handover_count: int
    scheduler_weight_applied: Optional[float] = None


# Control action structures
@dataclass
class ControlAction:
    type: str  # SCHEDULER_POLICY | MCS_CAP | PRB_WEIGHT | SLICE_QOS | REPORTING
    scope: str  # CELL | UE | SLICE
    cell_id: Optional[str] = None
    ue_id: Optional[str] = None
    slice_id: Optional[str] = None
    params: Dict[str, Any] = field(default_factory=dict)


# Learning loop DTOs
@dataclass
class DeviationSignal:
    intent_id: str
    metric: str  # e.g., THROUGHPUT_DL, DELAY_P95
    scope: str   # NETWORK | CELL | UE | SLICE
    cell_id: Optional[str]
    ue_id: Optional[str]
    predicted_time: datetime
    predicted_value: float
    threshold: float
    confidence: float  # [0,1]
    horizon_ms: int


@dataclass
class CandidateAction:
    actions: List[ControlAction]
    rationale: str
    expected_effects: List[Dict[str, Any]]


@dataclass
class DecisionRecord:
    header_sequence: int
    intent_id: str
    selected: Optional[CandidateAction]
    score: float
    applied_at: datetime
    correlation_id: str


@dataclass
class OutcomeRecord:
    correlation_id: str
    observed_window_start: datetime
    observed_window_end: datetime
    realized_metrics: Dict[str, float]
    error: Optional[str] = None
