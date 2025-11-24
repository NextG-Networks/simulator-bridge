#!/usr/bin/env python3
"""
Verify Grafana and InfluxDB setup
Checks if data is being written to InfluxDB and if the configuration is correct
"""

import os
import sys
from influxdb_client import InfluxDBClient
from influxdb_client.client.exceptions import InfluxDBError

# Configuration (should match csv_to_influxdb.py and docker-compose.yml)
INFLUXDB_URL = os.getenv("INFLUXDB_URL", "http://localhost:8086")
INFLUXDB_TOKEN = os.getenv("INFLUXDB_TOKEN", "my-super-secret-auth-token")
INFLUXDB_ORG = os.getenv("INFLUXDB_ORG", "ns3-org")
INFLUXDB_BUCKET = os.getenv("INFLUXDB_BUCKET", "ns3-kpis")

def check_influxdb_connection():
    """Check if InfluxDB is accessible"""
    print("=" * 60)
    print("Checking InfluxDB Connection...")
    print("=" * 60)
    
    try:
        client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
        health = client.health()
        print(f"✅ InfluxDB is accessible at {INFLUXDB_URL}")
        print(f"   Status: {health.status}")
        return client
    except Exception as e:
        print(f"❌ Cannot connect to InfluxDB: {e}")
        print(f"   URL: {INFLUXDB_URL}")
        print(f"   Make sure InfluxDB is running: docker compose up -d")
        return None

def check_bucket(client):
    """Check if the bucket exists"""
    print("\n" + "=" * 60)
    print("Checking Bucket...")
    print("=" * 60)
    
    try:
        buckets_api = client.buckets_api()
        buckets_result = buckets_api.find_buckets()
        
        # Handle both Buckets object and list
        if hasattr(buckets_result, 'buckets'):
            buckets = buckets_result.buckets
        elif hasattr(buckets_result, '__iter__'):
            buckets = list(buckets_result)
        else:
            buckets = []
        
        bucket_names = [b.name for b in buckets]
        print(f"Available buckets: {bucket_names}")
        
        if INFLUXDB_BUCKET in bucket_names:
            print(f"✅ Bucket '{INFLUXDB_BUCKET}' exists")
            return True
        else:
            print(f"❌ Bucket '{INFLUXDB_BUCKET}' not found")
            print(f"   Expected bucket: {INFLUXDB_BUCKET}")
            print(f"   Available buckets: {bucket_names}")
            return False
    except Exception as e:
        print(f"❌ Error checking bucket: {e}")
        import traceback
        traceback.print_exc()
        return False

def check_measurements(client):
    """Check if measurements exist in the bucket"""
    print("\n" + "=" * 60)
    print("Checking Measurements (Data)...")
    print("=" * 60)
    
    try:
        query_api = client.query_api()
        
        # Query for measurements
        query = f'''
        import "influxdata/influxdb/schema"
        schema.measurements(bucket: "{INFLUXDB_BUCKET}")
        '''
        
        result = query_api.query(org=INFLUXDB_ORG, query=query)
        
        measurements = []
        for table in result:
            for record in table.records:
                measurements.append(record.get_value())
        
        if measurements:
            print(f"✅ Found {len(measurements)} measurement(s):")
            for m in measurements:
                print(f"   - {m}")
        else:
            print("❌ No measurements found in bucket")
            print("   This means no data has been written yet.")
            print("   Make sure:")
            print("   1. CSV files exist (gnb_kpis.csv, ue_kpis.csv)")
            print("   2. csv_to_influxdb.py is running")
            print("   3. CSV files are being updated by the simulation")
            return False
        
        # Check for specific measurements we expect
        expected = ["gnb_kpis", "ue_kpis"]
        found = [m for m in expected if m in measurements]
        missing = [m for m in expected if m not in measurements]
        
        if found:
            print(f"\n✅ Expected measurements found: {found}")
        if missing:
            print(f"⚠️  Expected measurements missing: {missing}")
        
        return len(found) > 0
        
    except Exception as e:
        print(f"❌ Error checking measurements: {e}")
        return False

def check_recent_data(client):
    """Check if there's recent data"""
    print("\n" + "=" * 60)
    print("Checking Recent Data...")
    print("=" * 60)
    
    try:
        query_api = client.query_api()
        
        # First check all time to see if any data exists
        query_all = f'''
        from(bucket: "{INFLUXDB_BUCKET}")
          |> range(start: -30d)
          |> limit(n: 10)
        '''
        
        result_all = query_api.query(org=INFLUXDB_ORG, query=query_all)
        
        count_all = 0
        for table in result_all:
            for record in table.records:
                count_all += 1
        
        if count_all > 0:
            print(f"✅ Found {count_all} data point(s) in the last 30 days")
            
            # Now check last hour
            query_recent = f'''
            from(bucket: "{INFLUXDB_BUCKET}")
              |> range(start: -1h)
              |> limit(n: 10)
            '''
            
            result_recent = query_api.query(org=INFLUXDB_ORG, query=query_recent)
            
            count_recent = 0
            for table in result_recent:
                for record in table.records:
                    count_recent += 1
                    if count_recent <= 3:  # Show first 3 records
                        print(f"   Recent: measurement={record.get_measurement()}, "
                              f"field={record.get_field()}, "
                              f"time={record.get_time()}, "
                              f"value={record.get_value()}")
            
            if count_recent > 0:
                print(f"✅ Found {count_recent} data point(s) in the last hour")
                return True
            else:
                print(f"⚠️  No data in last hour, but {count_all} data point(s) exist (older data)")
                print("   This is OK if simulation hasn't run recently")
                return True  # Still return True since data exists
        else:
            print("❌ No data points found at all")
            print("   This means csv_to_influxdb.py hasn't written any data yet")
            return False
            
    except Exception as e:
        print(f"❌ Error checking recent data: {e}")
        import traceback
        traceback.print_exc()
        return False

def check_csv_files():
    """Check if CSV files exist"""
    print("\n" + "=" * 60)
    print("Checking CSV Files...")
    print("=" * 60)
    
    csv_gnb = "../gnb_kpis.csv"
    csv_ue = "../ue_kpis.csv"
    
    gnb_exists = os.path.exists(csv_gnb)
    ue_exists = os.path.exists(csv_ue)
    
    if gnb_exists:
        size = os.path.getsize(csv_gnb)
        print(f"✅ {csv_gnb} exists ({size} bytes)")
    else:
        print(f"❌ {csv_gnb} not found")
    
    if ue_exists:
        size = os.path.getsize(csv_ue)
        print(f"✅ {csv_ue} exists ({size} bytes)")
    else:
        print(f"❌ {csv_ue} not found")
    
    return gnb_exists or ue_exists

def main():
    """Main verification function"""
    print("\n" + "=" * 60)
    print("NS3 KPI Dashboard - Setup Verification")
    print("=" * 60)
    print(f"\nConfiguration:")
    print(f"  InfluxDB URL: {INFLUXDB_URL}")
    print(f"  Organization: {INFLUXDB_ORG}")
    print(f"  Bucket: {INFLUXDB_BUCKET}")
    print()
    
    # Check CSV files first
    csv_ok = check_csv_files()
    
    # Check InfluxDB connection
    client = check_influxdb_connection()
    if not client:
        print("\n❌ Cannot proceed without InfluxDB connection")
        sys.exit(1)
    
    # Check bucket
    bucket_ok = check_bucket(client)
    
    # Check measurements
    measurements_ok = check_measurements(client)
    
    # Check recent data
    data_ok = check_recent_data(client)
    
    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    
    if csv_ok:
        print("✅ CSV files: OK")
    else:
        print("❌ CSV files: Missing")
    
    if bucket_ok:
        print("✅ InfluxDB bucket: OK")
    else:
        print("❌ InfluxDB bucket: Missing or misconfigured")
    
    if measurements_ok:
        print("✅ Measurements: Found")
    else:
        print("❌ Measurements: Not found (no data written yet)")
    
    if data_ok:
        print("✅ Recent data: Found")
    else:
        print("❌ Recent data: Not found")
    
    print("\n" + "=" * 60)
    if measurements_ok and data_ok:
        print("✅ Setup looks good! Dashboard should work.")
        print("\nNext steps:")
        print("  1. Access Grafana: http://localhost:3000")
        print("  2. Login: admin / admin")
        print("  3. Navigate to Dashboards → NS3 KPI Dashboard")
    else:
        print("⚠️  Setup incomplete. Issues found above.")
        print("\nTroubleshooting:")
        if not csv_ok:
            print("  - Make sure CSV files are being generated by NS3 simulation")
        if not measurements_ok or not data_ok:
            print("  - Start csv_to_influxdb.py: python3 csv_to_influxdb.py")
            print("  - Make sure CSV files are being updated")
            print("  - Check csv_to_influxdb.py logs for errors")
    print("=" * 60 + "\n")
    
    client.close()

if __name__ == "__main__":
    main()

