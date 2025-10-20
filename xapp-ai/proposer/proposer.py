from __future__ import annotations
from typing import List

from xapp_ai.core.contracts import DeviationSignal, CandidateAction, ControlAction


class Proposer:
    def propose(self, signals: List[DeviationSignal], header, cells, ues) -> List[CandidateAction]:
        candidates: List[CandidateAction] = []
        for s in signals:
            if s.metric in {"DELAY_P95", "DELAY_P99", "BLER_DL", "BLER_UL"}:
                # propose lower mcs cap to improve reliability
                actions = [ControlAction(type="MCS_CAP", scope=s.scope, cell_id=s.cell_id, params={"dl_mcs_max": 18})]
                rationale = f"Reduce MCS to improve {s.metric}"
            else:
                # increase PRB weight to boost throughput/priority
                actions = [ControlAction(type="PRB_WEIGHT", scope=s.scope, cell_id=s.cell_id, ue_id=s.ue_id, params={"weight": 1.2})]
                rationale = f"Increase PRB weight to improve {s.metric}"
            candidates.append(CandidateAction(actions=actions, rationale=rationale, expected_effects=[{"metric": s.metric, "delta": 0.1, "horizon_ms": s.horizon_ms, "confidence": s.confidence}]))
        return candidates
