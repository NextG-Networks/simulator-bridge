from __future__ import annotations
from typing import List, Dict, Any
from datetime import timedelta

from xapp_ai.core.contracts import Header, CellMetrics, UEMetrics, DeviationSignal


class Observer:
    def observe(self, header: Header, cells: List[CellMetrics], ues: List[UEMetrics], intents: List[Dict[str, Any]]) -> List[DeviationSignal]:
        signals: List[DeviationSignal] = []
        horizon_ms = 2 * header.granularity_period_ms
        for intent in intents:
            metric = intent.get("metric")
            threshold = float(intent.get("threshold"))
            scope = intent.get("scope", "CELL")
            intent_id = intent.get("id", metric)
            # naive baseline: use last value as predicted value
            if scope == "CELL":
                for c in cells:
                    value = self._extract_cell_metric(c, metric)
                    if value is None:
                        continue
                    if self._violates(metric, value, threshold):
                        signals.append(DeviationSignal(
                            intent_id=intent_id,
                            metric=metric,
                            scope=scope,
                            cell_id=c.cell_id,
                            ue_id=None,
                            predicted_time=header.window_end + timedelta(milliseconds=horizon_ms),
                            predicted_value=value,
                            threshold=threshold,
                            confidence=0.6,
                            horizon_ms=horizon_ms,
                        ))
            elif scope == "UE":
                for u in ues:
                    value = self._extract_ue_metric(u, metric)
                    if value is None:
                        continue
                    if self._violates(metric, value, threshold):
                        signals.append(DeviationSignal(
                            intent_id=intent_id,
                            metric=metric,
                            scope=scope,
                            cell_id=u.cell_id,
                            ue_id=u.ue_id,
                            predicted_time=header.window_end + timedelta(milliseconds=horizon_ms),
                            predicted_value=value,
                            threshold=threshold,
                            confidence=0.6,
                            horizon_ms=horizon_ms,
                        ))
        return signals

    @staticmethod
    def _violates(metric: str, value: float, threshold: float) -> bool:
        # metrics where lower is better
        lower_is_better = {"DELAY_P95", "DELAY_P99", "BLER_DL", "BLER_UL", "JITTER_P95"}
        if metric in lower_is_better:
            return value > threshold
        # default higher is better
        return value < threshold

    @staticmethod
    def _extract_cell_metric(c: CellMetrics, metric: str):
        mapping = {
            "THROUGHPUT_DL": c.thr_dl_bps,
            "THROUGHPUT_UL": c.thr_ul_bps,
            "PRB_USED_DL": c.prb_used_dl,
            "PRB_USED_UL": c.prb_used_ul,
            "DELAY_P95": c.delay_p95_ms,
            "DELAY_P99": c.delay_p99_ms,
            "JITTER_P95": c.jitter_p95_ms,
            "BLER_DL": c.bler_dl,
            "BLER_UL": c.bler_ul,
        }
        return mapping.get(metric)

    @staticmethod
    def _extract_ue_metric(u: UEMetrics, metric: str):
        mapping = {
            "THROUGHPUT_DL": u.thr_dl_bps,
            "THROUGHPUT_UL": u.thr_ul_bps,
            "DELAY_P95": u.packet_delay_ms_p95,
            "DELAY_P99": u.packet_delay_ms_p99,
            "BLER_DL": u.bler_dl,
            "BLER_UL": u.bler_ul,
        }
        return mapping.get(metric)
