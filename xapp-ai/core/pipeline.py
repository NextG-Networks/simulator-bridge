from __future__ import annotations
from typing import List, Tuple, Dict, Any
from datetime import datetime

from .contracts import (
    Header,
    CellMetrics,
    UEMetrics,
    ControlAction,
    DecisionRecord,
)


class AIPipeline:
    def __init__(self, observer, proposer, predictor, actioner, learner, intents_registry, logger=None):
        self.observer = observer
        self.proposer = proposer
        self.predictor = predictor
        self.actioner = actioner
        self.learner = learner
        self.intents = intents_registry
        self.logger = logger

    def decide(self, header: Header, cells: List[CellMetrics], ues: List[UEMetrics]) -> Tuple[List[ControlAction], Dict[str, Any]]:
        correlation_id = f"seq-{header.sequence_number}"
        try:
            intents = self.intents.get_intents()
            signals = self.observer.observe(header, cells, ues, intents)
            candidates = self.proposer.propose(signals, header, cells, ues)
            selected, score = self.predictor.select(candidates, header, cells, ues)
            actions: List[ControlAction] = selected.actions if selected else []
            if actions:
                self.actioner.validate(actions)
            meta = {"correlation_id": correlation_id, "score": score}
            return actions, meta
        except Exception as e:
            if self.logger:
                self.logger.error(f"AIPipeline.decide error: {e}")
            return [], {"correlation_id": correlation_id, "error": str(e)}

    def feedback(self, decision: DecisionRecord, header: Header, cells: List[CellMetrics], ues: List[UEMetrics]):
        try:
            self.learner.update(decision, header, cells, ues)
        except Exception as e:
            if self.logger:
                self.logger.error(f"AIPipeline.feedback error: {e}")
