#!/usr/bin/env python3
"""
CSV to InfluxDB Bridge
Reads CSV files and writes data to InfluxDB for Grafana visualization

Usage:
    pip install influxdb-client pandas watchdog
    python3 csv_to_influxdb.py
"""

import os
import time
import pandas as pd
from datetime import datetime
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# Configuration
CSV_GNB_FILE = "../gnb_kpis.csv"
CSV_UE_FILE = "../ue_kpis.csv"

# InfluxDB configuration
INFLUXDB_URL = os.getenv("INFLUXDB_URL", "http://localhost:8086")
INFLUXDB_TOKEN = os.getenv("INFLUXDB_TOKEN", "my-super-secret-auth-token")
INFLUXDB_ORG = os.getenv("INFLUXDB_ORG", "ns3-org")
INFLUXDB_BUCKET = os.getenv("INFLUXDB_BUCKET", "ns3-kpis")

# Track last processed rows
last_gnb_row = 0
last_ue_row = 0

class CSVHandler(FileSystemEventHandler):
    """Handle CSV file changes"""
    
    def __init__(self, client, write_api):
        self.client = client
        self.write_api = write_api
        self.last_gnb_row = 0
        self.last_ue_row = 0
    
    def process_gnb_csv(self, file_path):
        """Process gNB CSV file and write to InfluxDB"""
        try:
            df = pd.read_csv(file_path)
            if df.empty:
                return
            
            # Process only new rows
            new_rows = df.iloc[self.last_gnb_row:]
            if new_rows.empty:
                return
            
            points = []
            for _, row in new_rows.iterrows():
                # Create a point for each row
                point = Point("gnb_kpis")
                
                # Add timestamp
                if 'timestamp' in row:
                    if pd.notna(row['timestamp']):
                        try:
                            # Try to parse as milliseconds timestamp
                            ts = pd.to_datetime(row['timestamp'], unit='ms', errors='coerce')
                            if pd.notna(ts):
                                point.time(ts)
                        except:
                            pass
                
                # Add tags (dimensions)
                if 'cell_id' in row and pd.notna(row['cell_id']) and str(row['cell_id']).upper() != 'N/A':
                    point.tag("cell_id", str(row['cell_id']))
                
                # Add fields (metrics)
                for col in df.columns:
                    if col not in ['timestamp', 'cell_id', 'meid', 'format']:
                        if pd.api.types.is_numeric_dtype(df[col]):
                            if pd.notna(row[col]):
                                point.field(col, float(row[col]))
                
                points.append(point)
            
            if points:
                self.write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
                self.last_gnb_row = len(df)
                print(f"[InfluxDB] Wrote {len(points)} gNB data points")
        
        except Exception as e:
            print(f"[InfluxDB] Error processing gNB CSV: {e}")
    
    def process_ue_csv(self, file_path):
        """Process UE CSV file and write to InfluxDB"""
        try:
            df = pd.read_csv(file_path)
            if df.empty:
                return
            
            # Process only new rows
            new_rows = df.iloc[self.last_ue_row:]
            if new_rows.empty:
                return
            
            points = []
            for _, row in new_rows.iterrows():
                # Create a point for each row
                point = Point("ue_kpis")
                
                # Add timestamp
                if 'timestamp' in row:
                    if pd.notna(row['timestamp']):
                        try:
                            ts = pd.to_datetime(row['timestamp'], unit='ms', errors='coerce')
                            if pd.notna(ts):
                                point.time(ts)
                        except:
                            pass
                
                # Add tags (dimensions)
                if 'ue_id' in row and pd.notna(row['ue_id']) and str(row['ue_id']).upper() != 'N/A':
                    point.tag("ue_id", str(row['ue_id']))
                if 'cell_id' in row and pd.notna(row['cell_id']) and str(row['cell_id']).upper() != 'N/A':
                    point.tag("cell_id", str(row['cell_id']))
                
                # Add fields (metrics)
                for col in df.columns:
                    if col not in ['timestamp', 'ue_id', 'cell_id', 'meid']:
                        if pd.api.types.is_numeric_dtype(df[col]):
                            if pd.notna(row[col]):
                                point.field(col, float(row[col]))
                
                points.append(point)
            
            if points:
                self.write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
                self.last_ue_row = len(df)
                print(f"[InfluxDB] Wrote {len(points)} UE data points")
        
        except Exception as e:
            print(f"[InfluxDB] Error processing UE CSV: {e}")
    
    def on_modified(self, event):
        """Handle file modification events"""
        if event.is_directory:
            return
        
        file_path = event.src_path
        
        if file_path.endswith('gnb_kpis.csv') or os.path.basename(file_path) == 'gnb_kpis.csv':
            if os.path.exists(CSV_GNB_FILE):
                self.process_gnb_csv(CSV_GNB_FILE)
        
        elif file_path.endswith('ue_kpis.csv') or os.path.basename(file_path) == 'ue_kpis.csv':
            if os.path.exists(CSV_UE_FILE):
                self.process_ue_csv(CSV_UE_FILE)

def main():
    """Main function to set up file watching and InfluxDB connection"""
    print(f"[InfluxDB] Connecting to {INFLUXDB_URL}...")
    
    try:
        client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
        write_api = client.write_api(write_options=SYNCHRONOUS)
        
        # Test connection
        health = client.health()
        print(f"[InfluxDB] Connected! Status: {health.status}")
        
        # Create handler
        event_handler = CSVHandler(client, write_api)
        
        # Process existing files
        if os.path.exists(CSV_GNB_FILE):
            print(f"[InfluxDB] Processing existing gNB CSV...")
            event_handler.process_gnb_csv(CSV_GNB_FILE)
        
        if os.path.exists(CSV_UE_FILE):
            print(f"[InfluxDB] Processing existing UE CSV...")
            event_handler.process_ue_csv(CSV_UE_FILE)
        
        # Set up file watcher
        observer = Observer()
        watch_dir = os.path.dirname(os.path.abspath(CSV_GNB_FILE))
        observer.schedule(event_handler, watch_dir, recursive=False)
        observer.start()
        
        print(f"[InfluxDB] Watching for CSV changes in {watch_dir}...")
        print(f"[InfluxDB] Press Ctrl+C to stop")
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n[InfluxDB] Stopping...")
            observer.stop()
        
        observer.join()
        client.close()
        
    except Exception as e:
        print(f"[InfluxDB] Error: {e}")
        print(f"[InfluxDB] Make sure InfluxDB is running and configured correctly")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())

