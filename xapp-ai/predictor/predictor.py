from __future__ import annotations
from typing import List, Tuple

from xapp_ai.core.contracts import CandidateAction


class Predictor:
    def select(self, candidates: List[CandidateAction], header, cells, ues) -> Tuple[CandidateAction | None, float]:
        if not candidates:
            return None, 0.0
        # naive baseline: pick first
        return candidates[0], 0.5
