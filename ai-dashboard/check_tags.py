#!/usr/bin/env python3
"""
Check if tags are being written to InfluxDB correctly
"""

import os
from influxdb_client import InfluxDBClient

INFLUXDB_URL = os.getenv("INFLUXDB_URL", "http://localhost:8086")
INFLUXDB_TOKEN = os.getenv("INFLUXDB_TOKEN", "my-super-secret-auth-token")
INFLUXDB_ORG = os.getenv("INFLUXDB_ORG", "ns3-org")
INFLUXDB_BUCKET = os.getenv("INFLUXDB_BUCKET", "ns3-kpis")

def check_tags():
    """Check what tags exist in InfluxDB"""
    print("=" * 60)
    print("Checking Tags in InfluxDB")
    print("=" * 60)
    
    try:
        client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
        query_api = client.query_api()
        
        # Check for tags in ue_kpis measurement
        print("\nChecking ue_kpis measurement...")
        query = f'''
        import "influxdata/influxdb/schema"
        
        schema.tagKeys(bucket: "{INFLUXDB_BUCKET}")
          |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
          |> distinct()
        '''
        
        result = query_api.query(org=INFLUXDB_ORG, query=query)
        
        tags = []
        for table in result:
            for record in table.records:
                tag_key = record.get_value()
                if tag_key:
                    tags.append(tag_key)
        
        if tags:
            print(f"✅ Found {len(tags)} tag key(s) in ue_kpis:")
            for tag in tags:
                print(f"   - {tag}")
        else:
            print("❌ No tag keys found in ue_kpis")
            print("   This means tags (cell_id, ue_id) are not being written")
        
        # Check for tag values
        if tags:
            print("\nChecking tag values...")
            for tag_key in tags:
                query = f'''
                import "influxdata/influxdb/schema"
                
                schema.tagValues(bucket: "{INFLUXDB_BUCKET}", tag: "{tag_key}")
                  |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
                  |> limit(n: 10)
                '''
                
                result = query_api.query(org=INFLUXDB_ORG, query=query)
                
                values = []
                for table in result:
                    for record in table.records:
                        value = record.get_value()
                        if value:
                            values.append(value)
                
                if values:
                    print(f"   {tag_key}: {len(values)} unique value(s) - {values[:5]}")
                else:
                    print(f"   {tag_key}: No values found")
        
        # Check for tags in gnb_kpis measurement
        print("\nChecking gnb_kpis measurement...")
        query = f'''
        import "influxdata/influxdb/schema"
        
        schema.tagKeys(bucket: "{INFLUXDB_BUCKET}")
          |> filter(fn: (r) => r["_measurement"] == "gnb_kpis")
          |> distinct()
        '''
        
        result = query_api.query(org=INFLUXDB_ORG, query=query)
        
        tags_gnb = []
        for table in result:
            for record in table.records:
                tag_key = record.get_value()
                if tag_key:
                    tags_gnb.append(tag_key)
        
        if tags_gnb:
            print(f"✅ Found {len(tags_gnb)} tag key(s) in gnb_kpis:")
            for tag in tags_gnb:
                print(f"   - {tag}")
        else:
            print("❌ No tag keys found in gnb_kpis")
        
        # Check sample data points
        print("\nChecking sample data points...")
        query = f'''
        from(bucket: "{INFLUXDB_BUCKET}")
          |> range(start: -30d)
          |> filter(fn: (r) => r["_measurement"] == "ue_kpis")
          |> limit(n: 3)
        '''
        
        result = query_api.query(org=INFLUXDB_ORG, query=query)
        
        count = 0
        for table in result:
            for record in table.records:
                count += 1
                if count <= 3:
                    print(f"\n   Data point {count}:")
                    print(f"      Measurement: {record.get_measurement()}")
                    print(f"      Field: {record.get_field()}")
                    print(f"      Time: {record.get_time()}")
                    print(f"      Value: {record.get_value()}")
                    print(f"      Tags: {dict(record.values)}")
        
        if count == 0:
            print("   ❌ No data points found")
        else:
            print(f"\n   ✅ Found {count} data point(s)")
        
        client.close()
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    check_tags()

