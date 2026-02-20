/**
 * @file Example2_MultipleModules.ino
 * @brief Example demonstrating multiple independent modules sharing one NVSConfigBus
 * 
 * This example shows how two independent modules ('pulsfan' and 'smartmifan') can
 * share the same NVSConfigBus instance while maintaining complete isolation.
 * 
 * It demonstrates:
 * - Multiple modules using the same bus instance
 * - Module isolation (clearing one doesn't affect the other)
 * - Factory reset functionality (clearAll)
 * - Helper functions for module-specific load/save operations
 */

#include <NVSConfigBus.h>
#include <ArduinoJson.h>

// Shared configuration bus for all modules
// All modules will use the same NVS namespace "appcfg"
NVSConfigBus configBus("appcfg");

// ============================================================================
// Pulsfan Module Configuration Helpers
// ============================================================================

/**
 * @brief Load pulsfan configuration, applying defaults if not found
 * @param doc JSON document to populate
 * @return true if config was loaded from NVS, false if defaults were applied
 */
bool loadPulsfanConfig(DynamicJsonDocument& doc) {
  if (!configBus.loadModuleConfig("pulsfan", doc)) {
    // Apply defaults
    doc["heartRateMin"] = 120;
    doc["heartRateMax"] = 180;
    doc["firmwareVersion"] = "1.0.0";
    doc["enabled"] = true;
    return false;  // Indicates defaults were applied
  }
  return true;  // Indicates loaded from NVS
}

/**
 * @brief Save pulsfan configuration to NVS
 * @param doc JSON document to save
 */
void savePulsfanConfig(const DynamicJsonDocument& doc) {
  if (configBus.saveModuleConfig("pulsfan", doc)) {
    Serial.println("  -> Pulsfan config saved");
  } else {
    Serial.println("  -> ERROR: Failed to save pulsfan config");
  }
}

// ============================================================================
// SmartMiFan Module Configuration Helpers
// ============================================================================

/**
 * @brief Load smartmifan configuration, applying defaults if not found
 * @param doc JSON document to populate
 * @return true if config was loaded from NVS, false if defaults were applied
 */
bool loadSmartMiFanConfig(DynamicJsonDocument& doc) {
  if (!configBus.loadModuleConfig("smartmifan", doc)) {
    // Apply defaults
    doc["fanIP"] = "192.168.1.100";
    doc["token"] = "";
    doc["port"] = 54321;
    
    // Create empty fans array
    JsonArray fans = doc.createNestedArray("fans");
    
    return false;  // Indicates defaults were applied
  }
  return true;  // Indicates loaded from NVS
}

/**
 * @brief Save smartmifan configuration to NVS
 * @param doc JSON document to save
 */
void saveSmartMiFanConfig(const DynamicJsonDocument& doc) {
  if (configBus.saveModuleConfig("smartmifan", doc)) {
    Serial.println("  -> SmartMiFan config saved");
  } else {
    Serial.println("  -> ERROR: Failed to save smartmifan config");
  }
}

// ============================================================================
// Main Setup
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== NVS Config Bus - Multiple Modules Example ===");
  Serial.println();

  // ========================================================================
  // Load both module configurations
  // ========================================================================
  
  Serial.println("--- Loading Module Configurations ---");
  
  // Load pulsfan configuration
  DynamicJsonDocument pulsfanDoc(512);
  bool pulsfanLoaded = loadPulsfanConfig(pulsfanDoc);
  Serial.print("Pulsfan config: ");
  Serial.println(pulsfanLoaded ? "loaded from NVS" : "defaults applied");
  Serial.print("  HR Min: ");
  Serial.println(pulsfanDoc["heartRateMin"].as<int>());
  Serial.print("  HR Max: ");
  Serial.println(pulsfanDoc["heartRateMax"].as<int>());
  Serial.print("  Enabled: ");
  Serial.println(pulsfanDoc["enabled"].as<bool>() ? "true" : "false");

  // Load smartmifan configuration
  DynamicJsonDocument fanDoc(512);
  bool fanLoaded = loadSmartMiFanConfig(fanDoc);
  Serial.print("SmartMiFan config: ");
  Serial.println(fanLoaded ? "loaded from NVS" : "defaults applied");
  Serial.print("  Fan IP: ");
  Serial.println(fanDoc["fanIP"].as<const char*>());
  Serial.print("  Port: ");
  Serial.println(fanDoc["port"].as<int>());
  Serial.print("  Token: ");
  const char* token = fanDoc["token"].as<const char*>();
  Serial.println(token && strlen(token) > 0 ? "***" : "(empty)");

  // ========================================================================
  // Modify configurations independently
  // ========================================================================
  
  Serial.println();
  Serial.println("--- Modifying Configurations ---");
  
  // Modify pulsfan config
  pulsfanDoc["heartRateMax"] = 200;
  pulsfanDoc["enabled"] = true;
  savePulsfanConfig(pulsfanDoc);
  Serial.println("  -> Updated pulsfan heartRateMax to 200");

  // Modify smartmifan config
  fanDoc["fanIP"] = "192.168.1.200";
  fanDoc["token"] = "abc123def456";
  JsonArray fans = fanDoc["fans"].as<JsonArray>();
  fans.clear();
  JsonObject fan1 = fans.createNestedObject();
  fan1["name"] = "Living Room Fan";
  fan1["token"] = "token1";
  JsonObject fan2 = fans.createNestedObject();
  fan2["name"] = "Bedroom Fan";
  fan2["token"] = "token2";
  saveSmartMiFanConfig(fanDoc);
  Serial.println("  -> Updated smartmifan IP and added 2 fans");

  // ========================================================================
  // Demonstrate module isolation
  // ========================================================================
  
  Serial.println();
  Serial.println("--- Testing Module Isolation ---");
  Serial.println("Clearing only pulsfan config...");
  
  if (configBus.clearModuleConfig("pulsfan")) {
    Serial.println("  -> Pulsfan config cleared");
  }

  // Verify smartmifan still exists
  DynamicJsonDocument fanDoc2(512);
  if (configBus.loadModuleConfig("smartmifan", fanDoc2)) {
    Serial.println("  -> SmartMiFan config still exists (isolation confirmed)");
    Serial.print("     Fan IP: ");
    Serial.println(fanDoc2["fanIP"].as<const char*>());
  } else {
    Serial.println("  -> ERROR: SmartMiFan config was lost (isolation failed!)");
  }

  // Verify pulsfan is gone
  DynamicJsonDocument pulsfanDoc2(512);
  if (configBus.loadModuleConfig("pulsfan", pulsfanDoc2)) {
    Serial.println("  -> ERROR: Pulsfan config still exists (clear failed!)");
  } else {
    Serial.println("  -> Pulsfan config confirmed cleared");
  }

  // ========================================================================
  // Factory reset demonstration
  // ========================================================================
  
  Serial.println();
  Serial.println("--- Factory Reset Demonstration ---");
  Serial.println("Performing factory reset (clearAll)...");
  
  if (configBus.clearAll()) {
    Serial.println("  -> All configuration cleared");
    
    // Reinitialize both modules with defaults
    Serial.println("  -> Reinitializing with defaults...");
    
    DynamicJsonDocument pulsfanDoc3(512);
    loadPulsfanConfig(pulsfanDoc3);
    savePulsfanConfig(pulsfanDoc3);
    
    DynamicJsonDocument fanDoc3(512);
    loadSmartMiFanConfig(fanDoc3);
    saveSmartMiFanConfig(fanDoc3);
    
    Serial.println("  -> Defaults restored for both modules");
  } else {
    Serial.println("  -> ERROR: Factory reset failed");
  }

  // ========================================================================
  // Final verification
  // ========================================================================
  
  Serial.println();
  Serial.println("--- Final Configuration State ---");
  
  DynamicJsonDocument finalPulsfan(512);
  if (configBus.loadModuleConfig("pulsfan", finalPulsfan)) {
    Serial.println("Pulsfan: exists with defaults");
  } else {
    Serial.println("Pulsfan: ERROR - not found");
  }
  
  DynamicJsonDocument finalFan(512);
  if (configBus.loadModuleConfig("smartmifan", finalFan)) {
    Serial.println("SmartMiFan: exists with defaults");
  } else {
    Serial.println("SmartMiFan: ERROR - not found");
  }

  Serial.println();
  Serial.println("=== Example complete ===");
  Serial.println("Both modules are isolated and can be managed independently.");
}

void loop() {
  // Your application code here
  // In a real application, each module would:
  // 1. Load its config once in setup()
  // 2. Use the values throughout the application
  // 3. Save config only when values change
  
  delay(10000);  // Prevent watchdog issues
}

