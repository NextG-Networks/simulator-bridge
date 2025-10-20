from __future__ import annotations

from xapp_ai.core.contracts import DecisionRecord, Header, CellMetrics, UEMetrics
from typing import List


class Learner:
    def update(self, decision: DecisionRecord, header: Header, cells: List[CellMetrics], ues: List[UEMetrics]):
        # baseline: no-op; replace with online learning updates
        return
