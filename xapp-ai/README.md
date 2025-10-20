# xApp AI Module

Purpose
- Provide a clean integration point for AI models embedded within the xApp.
- Define DTOs, pipeline stages, and offline replay tooling.

Quick Start
- Implement your components in observer/, proposer/, predictor/, actioner/, feedback/.
- Use core/contracts.py types for inputs/outputs.
- Run offline replay: python -m xapp_ai.tests.replay_driver --input kpi.jsonl

KPI Snapshot (canonical in-process contract)
- Header
  - ric_instance_id: string
  - function_id: string
  - kpm_version: string
  - granularity_period_ms: int
  - window_start: datetime
  - window_end: datetime
  - sequence_number: int
- CellMetrics
  - cell_id: string
  - prb_used_dl: int
  - prb_used_ul: int
  - prb_total: int
  - thr_dl_bps: float
  - thr_ul_bps: float
  - bler_dl: float
  - bler_ul: float
  - cqi_avg: float
  - mcs_dl_avg: float
  - mcs_ul_avg: float
  - active_ue_count: int
  - pdcp_pdu_tx: int
  - pdcp_pdu_rx: int
  - rlc_sdu_tx: int
  - rlc_sdu_rx: int
  - delay_p50_ms: float
  - delay_p95_ms: float
  - delay_p99_ms: float
  - jitter_p95_ms: float
  - handovers_triggered: int
- UEMetrics
  - ue_id: string
  - cell_id: string
  - rsrp_dbm: float
  - rsrq_db: float
  - sinr_db: float
  - cqi_avg: float
  - mcs_dl_avg: float
  - mcs_ul_avg: float
  - thr_dl_bps: float
  - thr_ul_bps: float
  - bler_dl: float
  - bler_ul: float
  - buffer_bytes_avg: float
  - packet_delay_ms_p50: float
  - packet_delay_ms_p95: float
  - packet_delay_ms_p99: float
  - handover_count: int
  - scheduler_weight_applied: float | None

Control Actions (output from AI)
- ControlAction
  - type: enum { SCHEDULER_POLICY, MCS_CAP, PRB_WEIGHT, SLICE_QOS, REPORTING }
  - scope: enum { CELL, UE, SLICE }
  - cell_id?: string
  - ue_id?: string
  - slice_id?: string
  - params: dict (see below)
- Params
  - SCHEDULER_POLICY: { policy: "PF" | "RR" | "MAX_THROUGHPUT" | "CUSTOM", custom_params?: dict }
  - MCS_CAP: { dl_mcs_max?: int, ul_mcs_max?: int, dl_mcs_min?: int, ul_mcs_min?: int }
  - PRB_WEIGHT: { weight: float }
  - SLICE_QOS: { weight: float, gbr_bps?: float, mbr_bps?: float }
  - REPORTING: { granularity_period_ms: int, enable_ue_level: bool }

Pipeline stages
- Observer: predicts deviation signals against intents (horizon_ms)
- Proposer: maps deviation signals to candidate actions
- Predictor: scores/ranks candidates, selects best (or none)
- Actioner: applies selected action through xApp control path
- Feedback/Learner: correlates outcomes and updates model/policy

DTOs for learning loop
- DeviationSignal: { intent_id, metric, scope, ids, predicted_time, predicted_value, threshold, confidence, horizon_ms }
- CandidateAction: { actions: ControlAction[], rationale, expected_effects }
- DecisionRecord: { header_sequence, intent_id, selected, score, applied_at, correlation_id }
- OutcomeRecord: { correlation_id, observed_window_start/end, realized_metrics, error }

Performance budget
- decide() must complete within granularity_period_ms/2 (e.g., under 500 ms for 1 s windows).
- Use non-blocking I/O and caching. Heavy training should be offline.

Offline replay
- Input: JSON Lines file of KPIIndication snapshots (mirrors top-level README schema).
- Run: python -m xapp_ai.tests.replay_driver --input kpi.jsonl --intents intents.yaml --out decisions.jsonl

Extension points
- Implement classes matching the abstract interfaces in each module.
- Configure via ai.config_path (YAML/JSON), hot-reload supported optionally.

Safety rails
- The xApp clamps MCS [0,28], PRB weights [0.0,10.0], and reporting period [100,5000] ms.
- Unknown UE IDs are ignored with warnings.

Outputs
- List[ControlAction] and metadata { correlation_id } per KPI window.
