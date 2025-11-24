#!/usr/bin/env python3
"""
One-time script to import existing CSV data into InfluxDB
Use this to import historical data from CSV files
"""

import os
import pandas as pd
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# Configuration (should match csv_to_influxdb.py)
CSV_GNB_FILE = "../gnb_kpis.csv"
CSV_UE_FILE = "../ue_kpis.csv"

INFLUXDB_URL = os.getenv("INFLUXDB_URL", "http://localhost:8086")
INFLUXDB_TOKEN = os.getenv("INFLUXDB_TOKEN", "my-super-secret-auth-token")
INFLUXDB_ORG = os.getenv("INFLUXDB_ORG", "ns3-org")
INFLUXDB_BUCKET = os.getenv("INFLUXDB_BUCKET", "ns3-kpis")

def import_gnb_csv(file_path):
    """Import gNB CSV file to InfluxDB"""
    if not os.path.exists(file_path):
        print(f"❌ File not found: {file_path}")
        return 0
    
    print(f"📄 Reading {file_path}...")
    df = pd.read_csv(file_path)
    
    if df.empty:
        print("   File is empty")
        return 0
    
    print(f"   Found {len(df)} rows")
    
    points = []
    for idx, row in df.iterrows():
        point = Point("gnb_kpis")
        
        # Add timestamp
        if 'timestamp' in row and pd.notna(row['timestamp']):
            try:
                ts = pd.to_datetime(row['timestamp'], unit='ms', errors='coerce')
                if pd.notna(ts):
                    point.time(ts)
            except:
                pass
        
        # Add tags
        if 'cell_id' in row and pd.notna(row['cell_id']) and str(row['cell_id']).upper() != 'N/A':
            point.tag("cell_id", str(row['cell_id']))
        
        # Add fields
        for col in df.columns:
            if col not in ['timestamp', 'cell_id', 'meid', 'format']:
                if pd.api.types.is_numeric_dtype(df[col]):
                    if pd.notna(row[col]):
                        point.field(col, float(row[col]))
        
        points.append(point)
        
        # Write in batches of 1000
        if len(points) >= 1000:
            write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
            print(f"   Wrote {len(points)} points... ({idx+1}/{len(df)})")
            points = []
    
    # Write remaining points
    if points:
        write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
        print(f"   Wrote {len(points)} points... (final batch)")
    
    return len(df)

def import_ue_csv(file_path):
    """Import UE CSV file to InfluxDB"""
    if not os.path.exists(file_path):
        print(f"❌ File not found: {file_path}")
        return 0
    
    print(f"📄 Reading {file_path}...")
    df = pd.read_csv(file_path)
    
    if df.empty:
        print("   File is empty")
        return 0
    
    print(f"   Found {len(df)} rows")
    
    points = []
    for idx, row in df.iterrows():
        point = Point("ue_kpis")
        
        # Add timestamp
        if 'timestamp' in row and pd.notna(row['timestamp']):
            try:
                ts = pd.to_datetime(row['timestamp'], unit='ms', errors='coerce')
                if pd.notna(ts):
                    point.time(ts)
            except:
                pass
        
        # Add tags
        if 'ue_id' in row and pd.notna(row['ue_id']) and str(row['ue_id']).upper() != 'N/A':
            point.tag("ue_id", str(row['ue_id']))
        if 'cell_id' in row and pd.notna(row['cell_id']) and str(row['cell_id']).upper() != 'N/A':
            point.tag("cell_id", str(row['cell_id']))
        
        # Add fields
        for col in df.columns:
            if col not in ['timestamp', 'ue_id', 'cell_id', 'meid']:
                if pd.api.types.is_numeric_dtype(df[col]):
                    if pd.notna(row[col]):
                        point.field(col, float(row[col]))
        
        points.append(point)
        
        # Write in batches of 1000
        if len(points) >= 1000:
            write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
            print(f"   Wrote {len(points)} points... ({idx+1}/{len(df)})")
            points = []
    
    # Write remaining points
    if points:
        write_api.write(bucket=INFLUXDB_BUCKET, org=INFLUXDB_ORG, record=points)
        print(f"   Wrote {len(points)} points... (final batch)")
    
    return len(df)

def main():
    print("=" * 60)
    print("CSV to InfluxDB - One-Time Import")
    print("=" * 60)
    print(f"\nConfiguration:")
    print(f"  InfluxDB URL: {INFLUXDB_URL}")
    print(f"  Organization: {INFLUXDB_ORG}")
    print(f"  Bucket: {INFLUXDB_BUCKET}")
    print()
    
    try:
        client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
        global write_api
        write_api = client.write_api(write_options=SYNCHRONOUS)
        
        # Test connection
        health = client.health()
        print(f"✅ Connected to InfluxDB (Status: {health.status})")
        print()
        
        # Import gNB data
        print("=" * 60)
        print("Importing gNB Data...")
        print("=" * 60)
        gnb_count = import_gnb_csv(CSV_GNB_FILE)
        
        print()
        
        # Import UE data
        print("=" * 60)
        print("Importing UE Data...")
        print("=" * 60)
        ue_count = import_ue_csv(CSV_UE_FILE)
        
        print()
        print("=" * 60)
        print("Import Complete!")
        print("=" * 60)
        print(f"✅ Imported {gnb_count} gNB rows")
        print(f"✅ Imported {ue_count} UE rows")
        print()
        print("You can now view the data in Grafana!")
        print("Access: http://localhost:3000")
        
        write_api.close()
        client.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())

