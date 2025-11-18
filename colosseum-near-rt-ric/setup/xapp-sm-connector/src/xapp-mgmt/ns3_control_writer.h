#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>

// Writes control commands to NS3 control files
// Supports: qos_actions.csv, ts_actions_for_ns3.csv, es_actions_for_ns3.csv
class Ns3ControlWriter {
public:
    // Initialize with base directory for control files
    Ns3ControlWriter(const std::string& base_dir = "/tmp");
    
    // Write QoS/PRB allocation commands
    // config_json format: {"type":"qos","commands":[{"ueId":"111000000000001","percentage":0.7},...]}
    bool WriteQosControl(const std::string& config_json);
    
    // Write handover commands
    // config_json format: {"type":"handover","commands":[{"imsi":"111000000000001","targetCellId":"1112"},...]}
    bool WriteHandoverControl(const std::string& config_json);
    
    // Write energy efficiency (cell on/off) commands
    // config_json format: {"type":"energy","commands":[{"cellId":"1112","hoAllowed":0},...]}
    bool WriteEnergyControl(const std::string& config_json);
    
    // Write eNB transmit power commands
    // config_json format: {"type":"set-enb-txpower","commands":[{"cellId":"1112","dbm":43.0},...]}
    bool WriteEnbTxPowerControl(const std::string& config_json);
    
    // Write UE transmit power commands
    // config_json format: {"type":"set-ue-txpower","commands":[{"ueId":"111000000000001","dbm":23.0},...]}
    bool WriteUeTxPowerControl(const std::string& config_json);
    
    // Write CBR traffic rate commands
    // config_json format: {"type":"set-cbr","rate":"50Mbps","pktBytes":1200}
    bool WriteCbrControl(const std::string& config_json);
    
    // Write PRB cap commands
    // config_json format: {"type":"cap-ue-prb","commands":[{"ueId":"111000000000001","maxPrb":10},...]}
    bool WritePrbCapControl(const std::string& config_json);
    
    // Generic handler that parses JSON and routes to appropriate writer
    bool WriteControl(const std::string& config_json);

private:
    std::string base_dir_;
    std::string qos_file_;
    std::string handover_file_;
    std::string energy_file_;
    std::string enb_txpower_file_;
    std::string ue_txpower_file_;
    std::string cbr_file_;
    std::string prb_cap_file_;
    
    // Helper to extract JSON field (simple parser, assumes well-formed JSON)
    std::string extractJsonField(const std::string& json, const std::string& field);
    std::vector<std::string> extractJsonArray(const std::string& json, const std::string& array_field);
    
    // Get current timestamp in milliseconds
    long long getTimestamp();
};

