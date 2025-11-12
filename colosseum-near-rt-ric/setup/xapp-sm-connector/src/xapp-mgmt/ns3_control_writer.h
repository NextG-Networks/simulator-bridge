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
    
    // Generic handler that parses JSON and routes to appropriate writer
    bool WriteControl(const std::string& config_json);

private:
    std::string base_dir_;
    std::string qos_file_;
    std::string handover_file_;
    std::string energy_file_;
    
    // Helper to extract JSON field (simple parser, assumes well-formed JSON)
    std::string extractJsonField(const std::string& json, const std::string& field);
    std::vector<std::string> extractJsonArray(const std::string& json, const std::string& array_field);
    
    // Get current timestamp in milliseconds
    long long getTimestamp();
};

