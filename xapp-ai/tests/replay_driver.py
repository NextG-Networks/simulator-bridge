from __future__ import annotations
import json
import argparse
from datetime import datetime

from xapp_ai.core.contracts import Header, CellMetrics, UEMetrics
from xapp_ai.core.pipeline import AIPipeline
from xapp_ai.intents.registry import IntentsRegistry
from xapp_ai.observer.observer import Observer
from xapp_ai.proposer.proposer import Proposer
from xapp_ai.predictor.predictor import Predictor
from xapp_ai.actioner.actioner import Actioner
from xapp_ai.feedback.learner import Learner


def parse_header(h):
    return Header(
        ric_instance_id=h["ric_instance_id"],
        function_id=h["function_id"],
        kpm_version=h.get("kpm_version", "2.0"),
        granularity_period_ms=int(h["granularity_period_ms"]),
        window_start=datetime.fromisoformat(h["window_start"].replace("Z", "+00:00")),
        window_end=datetime.fromisoformat(h["window_end"].replace("Z", "+00:00")),
        sequence_number=int(h["sequence_number"]),
    )


def parse_cell(c):
    m = c["metrics"]
    return CellMetrics(
        cell_id=c["cell_id"],
        prb_used_dl=int(m.get("prb_used_dl", 0)),
        prb_used_ul=int(m.get("prb_used_ul", 0)),
        prb_total=int(m.get("prb_total", 0)),
        thr_dl_bps=float(m.get("thr_dl_bps", 0.0)),
        thr_ul_bps=float(m.get("thr_ul_bps", 0.0)),
        bler_dl=float(m.get("bler_dl", 0.0)),
        bler_ul=float(m.get("bler_ul", 0.0)),
        cqi_avg=float(m.get("cqi_avg", 0.0)),
        mcs_dl_avg=float(m.get("mcs_dl_avg", 0.0)),
        mcs_ul_avg=float(m.get("mcs_ul_avg", 0.0)),
        active_ue_count=int(m.get("active_ue_count", 0)),
        pdcp_pdu_tx=int(m.get("pdcp_pdu_tx", 0)),
        pdcp_pdu_rx=int(m.get("pdcp_pdu_rx", 0)),
        rlc_sdu_tx=int(m.get("rlc_sdu_tx", 0)),
        rlc_sdu_rx=int(m.get("rlc_sdu_rx", 0)),
        delay_p50_ms=float(m.get("delay_p50_ms", 0.0)),
        delay_p95_ms=float(m.get("delay_p95_ms", 0.0)),
        delay_p99_ms=float(m.get("delay_p99_ms", 0.0)),
        jitter_p95_ms=float(m.get("jitter_p95_ms", 0.0)),
        handovers_triggered=int(m.get("handovers_triggered", 0)),
    )


def parse_ue(u):
    m = u["metrics"]
    return UEMetrics(
        ue_id=u["ue_id"],
        cell_id=u["cell_id"],
        rsrp_dbm=float(m.get("rsrp_dbm", 0.0)),
        rsrq_db=float(m.get("rsrq_db", 0.0)),
        sinr_db=float(m.get("sinr_db", 0.0)),
        cqi_avg=float(m.get("cqi_avg", 0.0)),
        mcs_dl_avg=float(m.get("mcs_dl_avg", 0.0)),
        mcs_ul_avg=float(m.get("mcs_ul_avg", 0.0)),
        thr_dl_bps=float(m.get("thr_dl_bps", 0.0)),
        thr_ul_bps=float(m.get("thr_ul_bps", 0.0)),
        bler_dl=float(m.get("bler_dl", 0.0)),
        bler_ul=float(m.get("bler_ul", 0.0)),
        buffer_bytes_avg=float(m.get("buffer_bytes_avg", 0.0)),
        packet_delay_ms_p50=float(m.get("packet_delay_ms_p50", 0.0)),
        packet_delay_ms_p95=float(m.get("packet_delay_ms_p95", 0.0)),
        packet_delay_ms_p99=float(m.get("packet_delay_ms_p99", 0.0)),
        handover_count=int(m.get("handover_count", 0)),
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Path to KPI JSONL file")
    parser.add_argument("--intents", required=False, help="Path to intents JSON/YAML")
    parser.add_argument("--out", required=False, default="decisions.jsonl", help="Output decisions JSONL")
    args = parser.parse_args()

    intents = IntentsRegistry(args.intents) if args.intents else IntentsRegistry()

    pipeline = AIPipeline(
        observer=Observer(), proposer=Proposer(), predictor=Predictor(), actioner=Actioner(), learner=Learner(), intents_registry=intents
    )

    with open(args.input, "r", encoding="utf-8") as fin, open(args.out, "w", encoding="utf-8") as fout:
        for line in fin:
            if not line.strip():
                continue
            snap = json.loads(line)
            header = parse_header(snap["header"])
            cells = [parse_cell(c) for c in snap.get("cells", [])]
            ues = [parse_ue(u) for u in snap.get("ues", [])]
            actions, meta = pipeline.decide(header, cells, ues)
            rec = {"correlation_id": meta.get("correlation_id"), "actions": [a.__dict__ for a in actions], "meta": meta}
            fout.write(json.dumps(rec) + "\n")


if __name__ == "__main__":
    main()
