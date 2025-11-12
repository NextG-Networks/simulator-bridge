#include "ns3_control_writer.h"
#include <iostream>
#include <regex>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" {
#include "mdclog/mdclog.h"
}

Ns3ControlWriter::Ns3ControlWriter(const std::string& base_dir)
    : base_dir_(base_dir),
      qos_file_(base_dir + "/qos_actions.csv"),
      handover_file_(base_dir + "/ts_actions_for_ns3.csv"),
      energy_file_(base_dir + "/es_actions_for_ns3.csv")
{
    // Ensure base directory exists (non-blocking, use filesystem API instead of system call)
    // Note: /tmp should already exist, but create if needed
    if (base_dir_ != "/tmp") {
        std::string cmd = "mkdir -p " + base_dir_ + " 2>/dev/null";
        std::system(cmd.c_str());
    }
}

long long Ns3ControlWriter::getTimestamp() {
    return static_cast<long long>(std::time(nullptr) * 1000); // milliseconds
}

std::string Ns3ControlWriter::extractJsonField(const std::string& json, const std::string& field) {
    // Simple JSON field extractor using regex
    // Format: "field":"value" or "field":value
    std::string pattern = "\"" + field + "\"\\s*:\\s*\"([^\"]+)\"";
    std::regex re(pattern);
    std::smatch match;
    
    if (std::regex_search(json, match, re)) {
        return match[1].str();
    }
    
    // Try numeric value
    pattern = "\"" + field + "\"\\s*:\\s*([0-9.]+)";
    re = std::regex(pattern);
    if (std::regex_search(json, match, re)) {
        return match[1].str();
    }
    
    return "";
}

std::vector<std::string> Ns3ControlWriter::extractJsonArray(const std::string& json, const std::string& array_field) {
    std::vector<std::string> result;
    
    // Find the array field
    std::string pattern = "\"" + array_field + "\"\\s*:\\s*\\[";
    std::regex re(pattern);
    std::smatch match;
    
    if (!std::regex_search(json, match, re)) {
        return result;
    }
    
    size_t start_pos = match.position() + match.length();
    size_t depth = 1;
    size_t i = start_pos;
    
    // Find matching closing bracket
    while (i < json.length() && depth > 0) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') depth--;
        i++;
    }
    
    if (depth == 0) {
        std::string array_content = json.substr(start_pos, i - start_pos - 1);
        
        // Split by objects (simple: find { ... } pairs)
        size_t obj_start = 0;
        depth = 0;
        for (size_t j = 0; j < array_content.length(); j++) {
            if (array_content[j] == '{') {
                if (depth == 0) obj_start = j;
                depth++;
            } else if (array_content[j] == '}') {
                depth--;
                if (depth == 0) {
                    result.push_back(array_content.substr(obj_start, j - obj_start + 1));
                }
            }
        }
    }
    
    return result;
}

// Helper function to convert IMSI string to RNTI (uint16_t)
// NS3 expects RNTI in the CSV, but AI sends IMSI.
// NS3 assigns RNTIs sequentially starting from 1, and IMSIs are also sequential.
// For IMSI format like "111000000000001" (PLMN=111, UE_ID=000000000001):
//   - Remove PLMN ID (first 3 digits "111")
//   - Extract the UE number part (last digits)
//   - For "111000000000001" -> remove "111" -> "000000000001" -> RNTI = 1
//   - For "111000000000002" -> remove "111" -> "000000000002" -> RNTI = 2
static uint16_t imsiToRnti(const std::string& imsi_str) {
    try {
        // If it's already a small number (likely RNTI), use it directly
        if (imsi_str.length() <= 5) {
            uint16_t rnti = static_cast<uint16_t>(std::stoul(imsi_str));
            if (rnti > 0 && rnti < 65535) {
                return rnti;
            }
        }
        
        // For IMSI format "111000000000001" (PLMN + UE_ID)
        // Remove PLMN ID (first 3 digits) and extract the UE number
        std::string imsi_clean = imsi_str;
        if (imsi_clean.length() > 3 && imsi_clean.substr(0, 3) == "111") {
            // Remove PLMN ID "111"
            imsi_clean = imsi_clean.substr(3);
        }
        
        // Convert remaining part to number (this is the UE number)
        // "000000000001" -> 1, "000000000002" -> 2, etc.
        uint64_t ue_num = std::stoull(imsi_clean);
        
        // RNTI should be the UE number (NS3 assigns RNTIs sequentially starting from 1)
        // But ensure it's within valid range
        if (ue_num == 0) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] IMSI %s resulted in RNTI 0, using 1", imsi_str.c_str());
            return 1;
        }
        if (ue_num > 65535) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] IMSI %s resulted in RNTI > 65535, using modulo", imsi_str.c_str());
            return static_cast<uint16_t>(ue_num % 65536);
        }
        
        return static_cast<uint16_t>(ue_num);
    } catch (const std::exception& e) {
        mdclog_write(MDCLOG_ERR, "[NS3-CTRL] Failed to convert IMSI to RNTI: %s - %s", 
                    imsi_str.c_str(), e.what());
        return 0; // Invalid
    }
}

bool Ns3ControlWriter::WriteQosControl(const std::string& config_json) {
    std::vector<std::string> commands = extractJsonArray(config_json, "commands");
    
    if (commands.empty()) {
        mdclog_write(MDCLOG_WARN, "[NS3-CTRL] No QoS commands found in JSON");
        return false;
    }
    
    std::ofstream csv(qos_file_, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        mdclog_write(MDCLOG_ERR, "[NS3-CTRL] Failed to open QoS control file: %s", qos_file_.c_str());
        return false;
    }
    
    long long timestamp = getTimestamp();
    int count = 0;
    
    for (const auto& cmd : commands) {
        std::string ue_id = extractJsonField(cmd, "ueId");
        std::string percentage = extractJsonField(cmd, "percentage");
        
        if (ue_id.empty() || percentage.empty()) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] Skipping invalid QoS command: %s", cmd.c_str());
            continue;
        }
        
        // Convert IMSI to RNTI (NS3 expects RNTI, not IMSI)
        uint16_t rnti = imsiToRnti(ue_id);
        if (rnti == 0) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] Failed to convert IMSI %s to RNTI, skipping", 
                        ue_id.c_str());
            continue;
        }
        
        // Validate percentage
        double perc = std::stod(percentage);
        if (perc < 0.0 || perc > 1.0) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] Invalid percentage %s for UE %s (RNTI %u), skipping", 
                        percentage.c_str(), ue_id.c_str(), rnti);
            continue;
        }
        
        // Write RNTI to CSV (NS3 expects uint16_t RNTI)
        csv << timestamp << "," << rnti << "," << percentage << "\n";
        count++;
        
        mdclog_write(MDCLOG_INFO, "[NS3-CTRL] QoS: IMSI=%s -> RNTI=%u, percentage=%s", 
                    ue_id.c_str(), rnti, percentage.c_str());
    }
    
    csv.close();
    mdclog_write(MDCLOG_INFO, "[NS3-CTRL] Wrote %d QoS commands to %s", count, qos_file_.c_str());
    return count > 0;
}

bool Ns3ControlWriter::WriteHandoverControl(const std::string& config_json) {
    std::vector<std::string> commands = extractJsonArray(config_json, "commands");
    
    if (commands.empty()) {
        mdclog_write(MDCLOG_WARN, "[NS3-CTRL] No handover commands found in JSON");
        return false;
    }
    
    std::ofstream csv(handover_file_, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        mdclog_write(MDCLOG_ERR, "[NS3-CTRL] Failed to open handover control file: %s", handover_file_.c_str());
        return false;
    }
    
    long long timestamp = getTimestamp();
    int count = 0;
    
    for (const auto& cmd : commands) {
        std::string imsi = extractJsonField(cmd, "imsi");
        std::string target_cell = extractJsonField(cmd, "targetCellId");
        
        if (imsi.empty() || target_cell.empty()) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] Skipping invalid handover command: %s", cmd.c_str());
            continue;
        }
        
        csv << timestamp << "," << imsi << "," << target_cell << "\n";
        count++;
        
        mdclog_write(MDCLOG_INFO, "[NS3-CTRL] Handover: IMSI=%s, targetCell=%s", 
                    imsi.c_str(), target_cell.c_str());
    }
    
    csv.close();
    mdclog_write(MDCLOG_INFO, "[NS3-CTRL] Wrote %d handover commands to %s", count, handover_file_.c_str());
    return count > 0;
}

bool Ns3ControlWriter::WriteEnergyControl(const std::string& config_json) {
    std::vector<std::string> commands = extractJsonArray(config_json, "commands");
    
    if (commands.empty()) {
        mdclog_write(MDCLOG_WARN, "[NS3-CTRL] No energy commands found in JSON");
        return false;
    }
    
    std::ofstream csv(energy_file_, std::ios::out | std::ios::trunc);
    if (!csv.is_open()) {
        mdclog_write(MDCLOG_ERR, "[NS3-CTRL] Failed to open energy control file: %s", energy_file_.c_str());
        return false;
    }
    
    long long timestamp = getTimestamp();
    int count = 0;
    
    for (const auto& cmd : commands) {
        std::string cell_id = extractJsonField(cmd, "cellId");
        std::string ho_allowed = extractJsonField(cmd, "hoAllowed");
        
        if (cell_id.empty() || ho_allowed.empty()) {
            mdclog_write(MDCLOG_WARN, "[NS3-CTRL] Skipping invalid energy command: %s", cmd.c_str());
            continue;
        }
        
        csv << timestamp << "," << cell_id << "," << ho_allowed << "\n";
        count++;
        
        mdclog_write(MDCLOG_INFO, "[NS3-CTRL] Energy: cellId=%s, hoAllowed=%s", 
                    cell_id.c_str(), ho_allowed.c_str());
    }
    
    csv.close();
    mdclog_write(MDCLOG_INFO, "[NS3-CTRL] Wrote %d energy commands to %s", count, energy_file_.c_str());
    return count > 0;
}

bool Ns3ControlWriter::WriteControl(const std::string& config_json) {
    std::string type = extractJsonField(config_json, "type");
    
    if (type == "qos") {
        return WriteQosControl(config_json);
    } else if (type == "handover" || type == "ts") {
        return WriteHandoverControl(config_json);
    } else if (type == "energy" || type == "es") {
        return WriteEnergyControl(config_json);
    } else {
        mdclog_write(MDCLOG_ERR, "[NS3-CTRL] Unknown control type: %s", type.c_str());
        return false;
    }
}

