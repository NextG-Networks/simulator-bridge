# Implementing RIC Control Message Actions for mmWave

This guide explains how to implement RIC control message actions that can change parameters in your `our.cc` scenario.

## Current State

Currently, RIC control messages are only processed in `LteEnbNetDevice::ControlMessageReceivedCallback()`. Since your `our.cc` scenario uses pure mmWave (no LTE), you need to:

1. **Add a control message callback to `MmWaveEnbNetDevice`**
2. **Extend the `RicControlMessage` enum** to support new action types
3. **Implement the action handlers** in the callback

## Step 1: Find and Understand RicControlMessage

The `RicControlMessage` class is likely defined in an E2-related header (possibly in `encode_e2apv1.hpp` or similar). It has:
- An enum `ControlMessageRequestIdType` with values like `TS` (Traffic Steering) and `QoS`
- Methods to extract control parameters from the E2AP PDU

## Step 2: Add ControlMessageReceivedCallback to MmWaveEnbNetDevice

### In `mmwave-enb-net-device.h`:

Add the callback declaration:
```cpp
void ControlMessageReceivedCallback(E2AP_PDU_t* sub_req_pdu);
```

### In `mmwave-enb-net-device.cc`:

Implement the callback (similar to `LteEnbNetDevice`):

```cpp
void
MmWaveEnbNetDevice::ControlMessageReceivedCallback(E2AP_PDU_t* sub_req_pdu)
{
    NS_LOG_DEBUG("MmWaveEnbNetDevice::ControlMessageReceivedCallback: Received RIC Control Message");

    Ptr<RicControlMessage> controlMessage = Create<RicControlMessage>(sub_req_pdu);
    NS_LOG_INFO("Request type " << controlMessage->m_requestType);
    
    switch (controlMessage->m_requestType)
    {
    case RicControlMessage::ControlMessageRequestIdType::TS: {
        // Handover action (if you have multiple cells)
        NS_LOG_INFO("TS (Traffic Steering) - Handover");
        // Extract IMSI and target cell ID from controlMessage
        // Perform handover via RRC
        break;
    }
    
    case RicControlMessage::ControlMessageRequestIdType::TX_POWER: {
        // NEW ACTION: Change transmit power
        NS_LOG_INFO("TX_POWER - Changing eNB transmit power");
        
        // Extract power value from control message
        // This requires extending RicControlMessage to parse power values
        double newPower = /* extract from controlMessage */;
        
        Ptr<MmWaveEnbPhy> phy = GetPhy();
        if (phy)
        {
            phy->SetTxPower(newPower);
            NS_LOG_INFO("Set eNB TX power to " << newPower << " dBm");
        }
        break;
    }
    
    case RicControlMessage::ControlMessageRequestIdType::BANDWIDTH: {
        // NEW ACTION: Change bandwidth
        NS_LOG_INFO("BANDWIDTH - Changing bandwidth");
        uint8_t newBw = /* extract from controlMessage */;
        SetBandwidth(newBw);
        NS_LOG_INFO("Set bandwidth to " << (unsigned)newBw);
        break;
    }
    
    case RicControlMessage::ControlMessageRequestIdType::CELL_STATE: {
        // NEW ACTION: Turn cell on/off/idle/sleep
        NS_LOG_INFO("CELL_STATE - Changing cell state");
        // Extract state from controlMessage
        // Use TurnOn(), TurnOff(), TurnIdle(), TurnSleep()
        break;
    }
    
    default: {
        NS_LOG_ERROR("Unrecognized RIC control message type: " << controlMessage->m_requestType);
        break;
    }
    }
}
```

## Step 3: Extend RicControlMessage Enum

You need to find where `ControlMessageRequestIdType` is defined and add new enum values:

```cpp
enum ControlMessageRequestIdType {
    TS,           // Traffic Steering (existing)
    QoS,          // Quality of Service (existing)
    TX_POWER,     // NEW: Transmit power control
    BANDWIDTH,    // NEW: Bandwidth control
    CELL_STATE,   // NEW: Cell state control (on/off/idle/sleep)
    MCS,          // NEW: MCS control
    // Add more as needed
};
```

## Step 4: Register the Callback

In your `our.cc` scenario or in the E2 termination setup, register the callback:

```cpp
// After creating the mmWave eNB device
Ptr<MmWaveEnbNetDevice> enbDev = gnbDevs.Get(0)->GetObject<MmWaveEnbNetDevice>();
Ptr<E2Termination> e2Term = enbDev->GetE2Termination();

if (e2Term)
{
    // Register control message callback
    e2Term->SetControlCallback(MakeCallback(&MmWaveEnbNetDevice::ControlMessageReceivedCallback, enbDev));
}
```

## Available Actions You Can Implement

Based on the mmWave eNB device capabilities, here are actions you can implement:

### 1. **Transmit Power Control** (`TX_POWER`)
- **What it changes**: eNB transmit power
- **Method**: `GetPhy()->SetTxPower(double power)`
- **Use case**: Interference management, coverage optimization, energy saving

### 2. **Bandwidth Control** (`BANDWIDTH`)
- **What it changes**: Carrier bandwidth
- **Method**: `SetBandwidth(uint8_t bw)`
- **Use case**: Dynamic spectrum allocation

### 3. **Cell State Control** (`CELL_STATE`)
- **What it changes**: Cell operational state
- **Methods**: `TurnOn()`, `TurnOff()`, `TurnIdle()`, `TurnSleep()`
- **Use case**: Energy saving, load balancing

### 4. **MCS Control** (`MCS`)
- **What it changes**: Modulation and Coding Scheme
- **Method**: Via scheduler attributes (requires scheduler modification)
- **Use case**: Link adaptation

### 5. **Handover Control** (`TS`)
- **What it changes**: UE cell association
- **Method**: Via RRC `PerformHandoverToTargetCell()`
- **Use case**: Load balancing, mobility optimization
- **Note**: Requires multiple cells in your scenario

### 6. **Scheduler Parameters** (`SCHEDULER`)
- **What it changes**: Scheduler behavior (HARQ, MCS, etc.)
- **Method**: Via scheduler attributes
- **Use case**: Performance optimization

## Step 5: Extend RicControlMessage to Parse New Parameters

You'll need to modify the `RicControlMessage` constructor or add getter methods to extract:
- Power values (for TX_POWER)
- Bandwidth values (for BANDWIDTH)
- State values (for CELL_STATE)
- etc.

This typically involves parsing the E2SM-RC control message format from the E2AP PDU.

## Example: Complete TX_POWER Action Implementation

```cpp
case RicControlMessage::ControlMessageRequestIdType::TX_POWER: {
    NS_LOG_INFO("TX_POWER - Changing eNB transmit power");
    
    // Extract power value from control message
    // This depends on how RicControlMessage is structured
    // You may need to add a method like: GetTxPowerValue()
    double newPower = controlMessage->GetTxPowerValue(); // or similar
    
    if (newPower < 0 || newPower > 50) {
        NS_LOG_WARN("Invalid TX power value: " << newPower);
        break;
    }
    
    Ptr<MmWaveEnbPhy> phy = GetPhy();
    if (phy) {
        phy->SetTxPower(newPower);
        NS_LOG_INFO("Successfully set eNB TX power to " << newPower << " dBm");
    } else {
        NS_LOG_ERROR("Failed to get PHY for TX power setting");
    }
    break;
}
```

## Important Notes

1. **E2 Message Format**: The actual parameter extraction depends on the E2SM-RC (E2 Service Model - RAN Control) message format. You'll need to understand how parameters are encoded in the E2AP PDU.

2. **Pure mmWave Limitation**: Since your scenario has no LTE eNB, control file reading won't work. RIC control messages via E2 are the proper way to control mmWave devices.

3. **Testing**: Test each action individually before combining them.

4. **Error Handling**: Always validate parameters and handle errors gracefully.

5. **Logging**: Use NS_LOG macros to track when actions are executed.

## Finding the RicControlMessage Definition

To find where `RicControlMessage` is defined:
```bash
grep -r "class RicControlMessage" src/
grep -r "ControlMessageRequestIdType" src/
```

It's likely in an E2-related header file or in the same directory as `lte-enb-net-device.cc`.

## Next Steps

1. Locate the `RicControlMessage` class definition
2. Understand its structure and how to extract parameters
3. Add the callback to `MmWaveEnbNetDevice`
4. Implement your desired actions
5. Test with E2 control messages from the RIC

