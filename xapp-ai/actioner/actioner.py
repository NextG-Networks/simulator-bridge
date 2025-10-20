from __future__ import annotations
from typing import List

from xapp_ai.core.contracts import ControlAction


class Actioner:
    def __init__(self, validator=None):
        self._validator = validator or self._default_validator

    def validate(self, actions: List[ControlAction]):
        for a in actions:
            self._validator(a)

    @staticmethod
    def _default_validator(a: ControlAction):
        if a.type == "MCS_CAP":
            for k in ("dl_mcs_max", "ul_mcs_max", "dl_mcs_min", "ul_mcs_min"):
                if k in a.params:
                    a.params[k] = max(0, min(28, int(a.params[k])))
        if a.type == "PRB_WEIGHT":
            if "weight" in a.params:
                a.params["weight"] = max(0.0, min(10.0, float(a.params["weight"])))
