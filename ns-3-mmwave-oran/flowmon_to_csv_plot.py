#!/usr/bin/env python3
"""
flowmon_to_csv_plot.py

Parse ns-3 FlowMonitor XML ("flowmon-results.xml" by default), export a CSV summary,
and create a bar chart of per-flow throughput.

Usage:
  python flowmon_to_csv_plot.py --input flowmon-results.xml --csv flowmon_summary.csv --png flowmon_throughput.png

If --csv/--png are omitted, defaults will be derived from the input name.
"""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
import math

import pandas as pd
import matplotlib.pyplot as plt


def safe_float(v, default=0.0):
    try:
        return float(v)
    except Exception:
        return default


def parse_flowmon(xml_path: Path):
    """
    Returns:
        df: pandas.DataFrame with per-flow metrics
    """
    tree = ET.parse(xml_path)
    root = tree.getroot()

    # Build classifier map: flowId -> five-tuple (may exist for IPv4 and/or IPv6)
    classifier = {}

    # IPv4 classifier
    for ipv4 in root.findall(".//Ipv4FlowClassifier/Flow"):
        fid = int(ipv4.attrib.get("flowId"))
        classifier[fid] = {
            "srcAddr": ipv4.attrib.get("sourceAddress"),
            "dstAddr": ipv4.attrib.get("destinationAddress"),
            "protocol": ipv4.attrib.get("protocol"),
            "srcPort": ipv4.attrib.get("sourcePort"),
            "dstPort": ipv4.attrib.get("destinationPort"),
            "version": "IPv4",
        }

    # IPv6 classifier (fallback if not present in IPv4)
    for ipv6 in root.findall(".//Ipv6FlowClassifier/Flow"):
        fid = int(ipv6.attrib.get("flowId"))
        if fid not in classifier:
            classifier[fid] = {
                "srcAddr": ipv6.attrib.get("sourceAddress"),
                "dstAddr": ipv6.attrib.get("destinationAddress"),
                "protocol": ipv6.attrib.get("protocol"),
                "srcPort": ipv6.attrib.get("sourcePort"),
                "dstPort": ipv6.attrib.get("destinationPort"),
                "version": "IPv6",
            }

    rows = []
    for flow in root.findall(".//FlowStats/Flow"):
        fid = int(flow.attrib.get("flowId"))

        txBytes = int(flow.attrib.get("txBytes", "0"))
        rxBytes = int(flow.attrib.get("rxBytes", "0"))
        txPackets = int(flow.attrib.get("txPackets", "0"))
        rxPackets = int(flow.attrib.get("rxPackets", "0"))
        lostPackets = int(flow.attrib.get("lostPackets", "0"))

        # Time fields
        t_first_tx = safe_float(flow.attrib.get("timeFirstTxPacket", "nan"), float("nan"))
        t_last_rx  = safe_float(flow.attrib.get("timeLastRxPacket", "nan"), float("nan"))
        duration = float("nan")
        if not math.isnan(t_first_tx) and not math.isnan(t_last_rx) and t_last_rx >= t_first_tx:
            duration = t_last_rx - t_first_tx

        # Delay/Jitter: FlowMonitor stores sums in seconds. We'll compute means in ms if possible.
        delay_sum  = safe_float(flow.attrib.get("delaySum", "0.0"), 0.0)  # seconds
        jitter_sum = safe_float(flow.attrib.get("jitterSum", "0.0"), 0.0) # seconds
        delay_mean_ms  = float("nan")
        jitter_mean_ms = float("nan")
        if rxPackets > 0:
            delay_mean_ms = (delay_sum / rxPackets) * 1e3
        if rxPackets > 1:
            jitter_mean_ms = (jitter_sum / (rxPackets - 1)) * 1e3

        # Throughput: use rxBytes and wall-clock duration between first TX and last RX
        throughput_mbps = float("nan")
        if duration and duration > 0:
            throughput_mbps = (rxBytes * 8.0) / duration / 1e6

        # Loss rate (if txPackets>0)
        loss_rate = float("nan")
        if txPackets > 0:
            loss_rate = lostPackets / txPackets

        five_tuple = classifier.get(fid, {})
        rows.append({
            "flowId": fid,
            "srcAddr": five_tuple.get("srcAddr"),
            "srcPort": five_tuple.get("srcPort"),
            "dstAddr": five_tuple.get("dstAddr"),
            "dstPort": five_tuple.get("dstPort"),
            "protocol": five_tuple.get("protocol"),
            "ipVersion": five_tuple.get("version", ""),
            "txPackets": txPackets,
            "rxPackets": rxPackets,
            "lostPackets": lostPackets,
            "txBytes": txBytes,
            "rxBytes": rxBytes,
            "duration_s": duration,
            "throughput_Mbps": throughput_mbps,
            "delayMean_ms": delay_mean_ms,
            "jitterMean_ms": jitter_mean_ms,
            "lossRate": loss_rate,
        })

    df = pd.DataFrame(rows).sort_values(by=["flowId"]).reset_index(drop=True)
    return df


def make_plot(df: pd.DataFrame, png_path: Path):
    """Create a bar chart of per-flow throughput (Mbps)."""
    # Only keep rows with finite throughput
    plot_df = df[pd.notnull(df["throughput_Mbps"])].copy()
    if plot_df.empty:
        print("No finite throughput values to plot; skipping chart.")
        return

    plt.figure()
    x = plot_df["flowId"].astype(str).tolist()
    y = plot_df["throughput_Mbps"].tolist()
    plt.bar(x, y)
    plt.title("Per-flow throughput (Mbps)")
    plt.xlabel("Flow ID")
    plt.ylabel("Throughput (Mbps)")
    plt.tight_layout()
    plt.savefig(png_path)
    plt.close()
    print(f"Saved plot: {png_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", "-i", type=str, default="flowmon-results.xml",
                    help="Path to FlowMonitor XML (default: flowmon-results.xml)")
    ap.add_argument("--csv", "-c", type=str, default=None,
                    help="Output CSV path (default: <input_basename>_summary.csv)")
    ap.add_argument("--png", "-p", type=str, default=None,
                    help="Output PNG path (default: <input_basename>_throughput.png)")
    args = ap.parse_args()

    xml_path = Path(args.input)
    if not xml_path.exists():
        raise SystemExit(f"Input XML not found: {xml_path}")

    # Defaults based on input name
    stem = xml_path.stem
    csv_path = Path(args.csv) if args.csv else xml_path.with_name(f"{stem}_summary.csv")
    png_path = Path(args.png) if args.png else xml_path.with_name(f"{stem}_throughput.png")

    df = parse_flowmon(xml_path)
    df.to_csv(csv_path, index=False)
    print(f"Saved CSV: {csv_path}")

    # Make a simple throughput bar chart
    make_plot(df, png_path)

    # Print a quick text summary top-N by throughput
    top = df.sort_values("throughput_Mbps", ascending=False).head(10)
    print("\nTop flows by throughput (Mbps):")
    print(top[["flowId", "srcAddr", "srcPort", "dstAddr", "dstPort", "throughput_Mbps"]].to_string(index=False))


if __name__ == "__main__":
    main()
