/**
 * @file Example3_SchemaMigration.ino
 * @brief Advanced example demonstrating schema versioning and migration
 * 
 * This example shows how to handle schema evolution by:
 * - Storing a schema_version field in the JSON document
 * - Detecting old schema versions on load
 * - Migrating old data formats to new ones
 * - Maintaining backward compatibility with older firmware versions
 * 
 * Migration Strategy:
 * - Each schema version can migrate from the previous version
 * - Unknown or missing schema versions trigger default initialization
 * - Future schema versions (newer than current) are preserved with a warning
 * - Migrations are atomic: read old, transform in RAM, write new
 */

#include <NVSConfigBus.h>
#include <ArduinoJson.h>

// Create configuration bus instance
NVSConfigBus configBus("appcfg");

// Current schema version - increment this when you change the data structure
const int CURRENT_SCHEMA_VERSION = 2;

// ============================================================================
// Default Configuration
// ============================================================================

/**
 * @brief Apply default configuration structure for current schema version
 * @param doc JSON document to populate with defaults
 */
void applyDefaultConfig(DynamicJsonDocument& doc) {
  doc["schema_version"] = CURRENT_SCHEMA_VERSION;
  doc["heartRateMin"] = 120;
  doc["heartRateMax"] = 180;
  doc["firmwareVersion"] = "1.0.0";
  doc["enabled"] = true;
  
  // Create empty tokens array
  JsonArray tokens = doc.createNestedArray("tokens");
  
  // Create settings object
  JsonObject settings = doc.createNestedObject("settings");
  settings["autoStart"] = false;
  settings["notifications"] = true;
}

// ============================================================================
// Migration Functions
// ============================================================================

/**
 * @brief Migrate from schema version 1 to version 2
 * 
 * Schema v1 structure:
 *   - hr_min (int)
 *   - hr_max (int)
 *   - version (string) - firmware version
 * 
 * Schema v2 structure:
 *   - heartRateMin (int) - renamed from hr_min
 *   - heartRateMax (int) - renamed from hr_max
 *   - firmwareVersion (string) - renamed from version
 *   - enabled (bool) - new field
 *   - tokens (array) - new field
 *   - settings (object) - new field with autoStart and notifications
 * 
 * @param doc JSON document to migrate (will be modified in place)
 * @return true if migration succeeded
 */
bool migrateFromV1ToV2(DynamicJsonDocument& doc) {
  Serial.println("  -> Migrating from schema v1 to v2");
  
  // Rename fields: old names -> new names
  if (doc.containsKey("hr_min")) {
    doc["heartRateMin"] = doc["hr_min"];
    doc.remove("hr_min");
    Serial.println("     Renamed hr_min -> heartRateMin");
  }
  
  if (doc.containsKey("hr_max")) {
    doc["heartRateMax"] = doc["hr_max"];
    doc.remove("hr_max");
    Serial.println("     Renamed hr_max -> heartRateMax");
  }
  
  if (doc.containsKey("version")) {
    doc["firmwareVersion"] = doc["version"];
    doc.remove("version");
    Serial.println("     Renamed version -> firmwareVersion");
  }
  
  // Add new fields introduced in v2
  if (!doc.containsKey("enabled")) {
    doc["enabled"] = true;
    Serial.println("     Added enabled field (default: true)");
  }
  
  if (!doc.containsKey("tokens")) {
    doc.createNestedArray("tokens");
    Serial.println("     Added tokens array");
  }
  
  if (!doc.containsKey("settings")) {
    JsonObject settings = doc.createNestedObject("settings");
    settings["autoStart"] = false;
    settings["notifications"] = true;
    Serial.println("     Added settings object");
  }
  
  // Update schema version
  doc["schema_version"] = CURRENT_SCHEMA_VERSION;
  Serial.println("     Updated schema_version to 2");
  
  return true;
}

/**
 * @brief Migrate from schema version 0 (legacy/unknown) to current
 * 
 * This handles cases where:
 * - schema_version field is missing
 * - schema_version is 0
 * - Data structure is completely unknown
 * 
 * Strategy: Apply defaults, but try to preserve any recognizable values
 * 
 * @param doc JSON document to migrate
 * @return true if migration succeeded
 */
bool migrateFromV0ToCurrent(DynamicJsonDocument& doc) {
  Serial.println("  -> Migrating from unknown/legacy schema");
  
  // Try to preserve any values that might be useful
  int heartRateMin = doc["heartRateMin"] | doc["hr_min"] | 120;
  int heartRateMax = doc["heartRateMax"] | doc["hr_max"] | 180;
  const char* firmwareVersion = doc["firmwareVersion"] | doc["version"] | "1.0.0";
  
  // Clear and rebuild with defaults
  doc.clear();
  applyDefaultConfig(doc);
  
  // Restore preserved values
  doc["heartRateMin"] = heartRateMin;
  doc["heartRateMax"] = heartRateMax;
  doc["firmwareVersion"] = firmwareVersion;
  
  Serial.println("     Applied defaults with preserved values");
  
  return true;
}

// ============================================================================
// Main Load Function with Migration
// ============================================================================

/**
 * @brief Load configuration with automatic schema migration
 * 
 * This function:
 * 1. Attempts to load configuration from NVS
 * 2. Checks the schema_version field
 * 3. Performs migrations if needed
 * 4. Saves migrated configuration back to NVS
 * 5. Returns true if config exists (new or migrated), false if defaults were applied
 * 
 * @param doc JSON document to populate
 * @return true if config was loaded/migrated, false if defaults were applied
 */
bool loadConfigWithMigration(DynamicJsonDocument& doc) {
  // Try to load existing configuration
  if (!configBus.loadModuleConfig("pulsfan", doc)) {
    Serial.println("No config found, applying defaults");
    applyDefaultConfig(doc);
    // Save defaults so they persist
    configBus.saveModuleConfig("pulsfan", doc);
    return false;  // New config created
  }

  // Check schema version
  int schemaVersion = doc["schema_version"] | 0;  // Default to 0 if missing

  Serial.print("Detected schema version: ");
  Serial.println(schemaVersion);

  if (schemaVersion == CURRENT_SCHEMA_VERSION) {
    Serial.println("  -> Config is up to date, no migration needed");
    return true;  // Already current version
  }

  // Perform migrations based on detected version
  bool migrationNeeded = false;

  if (schemaVersion == 1) {
    migrationNeeded = migrateFromV1ToV2(doc);
  } else if (schemaVersion == 0) {
    // Very old config or missing schema_version
    Serial.println("  -> Unknown schema version, applying defaults with preservation");
    migrationNeeded = migrateFromV0ToCurrent(doc);
  } else if (schemaVersion > CURRENT_SCHEMA_VERSION) {
    // Future schema version (device has newer firmware than expected)
    Serial.print("  -> WARNING: Config from future version (");
    Serial.print(schemaVersion);
    Serial.println(")");
    Serial.println("     Keeping config as-is, may have compatibility issues");
    // Don't modify the config, just return it
    return true;
  } else {
    // Unknown intermediate version - try to migrate through chain
    Serial.print("  -> Unknown intermediate version, attempting step-by-step migration");
    // For this example, we'll just apply defaults
    // In a real application, you might want to chain migrations:
    // v1 -> v2 -> v3 -> ... -> current
    migrationNeeded = migrateFromV0ToCurrent(doc);
  }

  if (migrationNeeded) {
    // Save migrated configuration
    if (configBus.saveModuleConfig("pulsfan", doc)) {
      Serial.println("  -> Migration complete, config saved");
    } else {
      Serial.println("  -> ERROR: Failed to save migrated config");
      return false;
    }
  }

  return true;
}

// ============================================================================
// Setup and Main Code
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== NVS Config Bus - Schema Migration Example ===");
  Serial.println();
  Serial.print("Current schema version: ");
  Serial.println(CURRENT_SCHEMA_VERSION);
  Serial.println();

  // Load configuration with automatic migration
  DynamicJsonDocument doc(1024);
  
  Serial.println("Loading configuration...");
  bool wasLoaded = loadConfigWithMigration(doc);
  Serial.print("Result: ");
  Serial.println(wasLoaded ? "loaded/migrated" : "defaults applied");
  Serial.println();

  // Display the configuration
  Serial.println("--- Current Configuration ---");
  Serial.print("Schema Version: ");
  Serial.println(doc["schema_version"].as<int>());
  Serial.print("Heart Rate Range: ");
  Serial.print(doc["heartRateMin"].as<int>());
  Serial.print(" - ");
  Serial.println(doc["heartRateMax"].as<int>());
  Serial.print("Firmware Version: ");
  Serial.println(doc["firmwareVersion"].as<const char*>());
  Serial.print("Enabled: ");
  Serial.println(doc["enabled"].as<bool>() ? "true" : "false");
  
  if (doc.containsKey("tokens")) {
    JsonArray tokens = doc["tokens"].as<JsonArray>();
    Serial.print("Tokens: ");
    Serial.print(tokens.size());
    Serial.println(" items");
  }
  
  if (doc.containsKey("settings")) {
    JsonObject settings = doc["settings"].as<JsonObject>();
    Serial.print("Settings - Auto Start: ");
    Serial.println(settings["autoStart"].as<bool>() ? "true" : "false");
    Serial.print("Settings - Notifications: ");
    Serial.println(settings["notifications"].as<bool>() ? "true" : "false");
  }

  Serial.println();
  Serial.println("=== Example complete ===");
  Serial.println();
  Serial.println("Migration Strategy Notes:");
  Serial.println("- Backward compatibility: Old devices upgrade automatically");
  Serial.println("- Forward compatibility: Newer schemas are preserved");
  Serial.println("- Default fallback: Unknown schemas trigger defaults");
  Serial.println("- Atomic updates: Migration is read -> transform -> write");
  Serial.println();
  Serial.println("To test migration:");
  Serial.println("1. First run creates v2 config");
  Serial.println("2. Manually create v1 config in NVS (or modify code)");
  Serial.println("3. Re-run to see migration in action");
}

void loop() {
  // Your application code here
  // In a real application:
  // 1. Load config once in setup() with migration
  // 2. Use the values throughout your application
  // 3. Save config only when values change
  // 4. When adding new fields, increment CURRENT_SCHEMA_VERSION and add migration
  
  delay(10000);  // Prevent watchdog issues
}

