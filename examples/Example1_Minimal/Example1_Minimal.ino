/**
 * @file Example1_Minimal.ino
 * @brief Minimal example demonstrating single module configuration storage
 * 
 * This example shows basic usage with a single module ('pulsfan') that stores:
 * - Heart rate min/max values
 * - Firmware version
 * - List of tokens
 * 
 * It demonstrates:
 * - Creating a global NVSConfigBus instance
 * - Loading configuration with default value application
 * - Modifying values and persisting changes
 */

#include <NVSConfigBus.h>
#include <ArduinoJson.h>

// Create a global configuration bus instance
// Using default namespace "appcfg"
NVSConfigBus configBus("appcfg");

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== NVS Config Bus - Minimal Example ===");
  Serial.println();

  // Allocate JSON document (adjust size based on your data)
  // For this example, 1024 bytes should be sufficient
  DynamicJsonDocument doc(1024);

  // Attempt to load existing configuration
  Serial.println("Attempting to load 'pulsfan' configuration...");
  if (!configBus.loadModuleConfig("pulsfan", doc)) {
    Serial.println("  -> No existing config found, applying defaults");
    
    // Apply default values
    doc["heartRateMin"] = 120;
    doc["heartRateMax"] = 180;
    doc["firmwareVersion"] = "1.0.0";
    
    // Create empty tokens array
    JsonArray tokens = doc.createNestedArray("tokens");
    
    // Save defaults
    if (configBus.saveModuleConfig("pulsfan", doc)) {
      Serial.println("  -> Default configuration saved successfully");
    } else {
      Serial.println("  -> ERROR: Failed to save default configuration");
      return;
    }
  } else {
    Serial.println("  -> Configuration loaded successfully");
    Serial.print("  -> Heart Rate Min: ");
    Serial.println(doc["heartRateMin"].as<int>());
    Serial.print("  -> Heart Rate Max: ");
    Serial.println(doc["heartRateMax"].as<int>());
    Serial.print("  -> Firmware Version: ");
    Serial.println(doc["firmwareVersion"].as<const char*>());
    
    // Display tokens if they exist
    if (doc.containsKey("tokens") && doc["tokens"].is<JsonArray>()) {
      JsonArray tokens = doc["tokens"].as<JsonArray>();
      Serial.print("  -> Tokens count: ");
      Serial.println(tokens.size());
      for (size_t i = 0; i < tokens.size(); i++) {
        Serial.print("     Token ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(tokens[i].as<const char*>());
      }
    }
  }

  Serial.println();
  Serial.println("Modifying configuration...");
  
  // Modify configuration values
  doc["heartRateMax"] = 200;
  Serial.println("  -> Updated heartRateMax to 200");
  
  // Add tokens to the array
  JsonArray tokens = doc["tokens"].as<JsonArray>();
  tokens.clear();  // Clear existing tokens
  tokens.add("token_abc123");
  tokens.add("token_def456");
  tokens.add("token_ghi789");
  Serial.println("  -> Added 3 tokens to array");
  
  // Save modified configuration
  if (configBus.saveModuleConfig("pulsfan", doc)) {
    Serial.println("  -> Configuration updated successfully");
  } else {
    Serial.println("  -> ERROR: Failed to save updated configuration");
    return;
  }

  Serial.println();
  Serial.println("Verifying saved configuration...");
  
  // Reload to verify
  DynamicJsonDocument verifyDoc(1024);
  if (configBus.loadModuleConfig("pulsfan", verifyDoc)) {
    Serial.println("  -> Configuration verified:");
    Serial.print("     Heart Rate Min: ");
    Serial.println(verifyDoc["heartRateMin"].as<int>());
    Serial.print("     Heart Rate Max: ");
    Serial.println(verifyDoc["heartRateMax"].as<int>());
    Serial.print("     Tokens count: ");
    JsonArray verifyTokens = verifyDoc["tokens"].as<JsonArray>();
    Serial.println(verifyTokens.size());
  }

  Serial.println();
  Serial.println("=== Example complete ===");
  Serial.println("Configuration persists across reboots.");
  Serial.println("To test, upload this sketch again and observe the loaded values.");
}

void loop() {
  // Your application code here
  // In a real application, you would:
  // 1. Load config once in setup()
  // 2. Use the values throughout your application
  // 3. Save config only when values change (not in loop())
  
  delay(10000);  // Prevent watchdog issues
}

