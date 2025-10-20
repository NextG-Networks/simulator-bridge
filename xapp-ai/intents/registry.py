from __future__ import annotations
from typing import Dict, Any, List
import json
import os


class IntentsRegistry:
    def __init__(self, path: str | None = None):
        self._path = path
        self._intents: List[Dict[str, Any]] = []
        if path and os.path.exists(path):
            self.load(path)

    def load(self, path: str):
        with open(path, "r", encoding="utf-8") as f:
            if path.endswith(".json"):
                self._intents = json.load(f)
            else:
                # minimal YAML support without dependency: treat as JSON superset if possible
                self._intents = json.loads(f.read())

    def get_intents(self) -> List[Dict[str, Any]]:
        return self._intents
