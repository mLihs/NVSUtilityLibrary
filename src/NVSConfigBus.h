/**
 * @file NVSConfigBus.h
 * @brief Centralized NVS configuration storage utility for ESP32 Arduino projects
 * 
 * This library provides a unified interface for storing and retrieving configuration
 * data for multiple independent modules using ESP32's NVS (Non-Volatile Storage).
 * Each module's configuration is stored as a JSON document, allowing flexible
 * schema evolution and easy integration with ArduinoJson.
 * 
 * @author Martin Lihs
 * @version 1.0.0
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Optional debug logging macro
#ifndef NVS_CFG_ENABLE_LOGGING
#define NVS_CFG_ENABLE_LOGGING 1
#endif

#if NVS_CFG_ENABLE_LOGGING
#define NVS_CFG_LOG(msg) Serial.print("[NVSConfigBus] "); Serial.println(msg)
#else
#define NVS_CFG_LOG(msg) ((void)0)
#endif

/**
 * @class NVSConfigBus
 * @brief Centralized configuration storage bus for multiple modules
 * 
 * This class provides a unified interface for storing and retrieving configuration
 * data for multiple independent modules. Each module is identified by a unique
 * moduleId string, and its configuration is stored as a JSON document in NVS.
 * 
 * Design principles:
 * - One namespace per NVSConfigBus instance (configurable via constructor)
 * - Each module stores its config as a JSON blob under its moduleId key
 * - Modules are isolated: clearing one module doesn't affect others
 * - Caller is responsible for applying default values when loading fails
 * 
 * @note This class is intended for configuration storage, not high-frequency logging.
 *       Avoid calling saveModuleConfig() in tight loops to minimize flash wear.
 * 
 * @example
 * ```cpp
 * NVSConfigBus configBus("appcfg");
 * DynamicJsonDocument doc(1024);
 * 
 * // Load module config
 * if (!configBus.loadModuleConfig("pulsfan", doc)) {
 *   // Apply defaults
 *   doc["heartRateMin"] = 120;
 *   doc["heartRateMax"] = 180;
 * }
 * 
 * // Modify and save
 * doc["heartRateMax"] = 200;
 * configBus.saveModuleConfig("pulsfan", doc);
 * ```
 */
class NVSConfigBus {
public:
  /**
   * @brief Construct a new NVSConfigBus instance
   * 
   * Creates a new configuration bus instance with the specified NVS namespace.
   * The namespace is used to isolate this bus's data from other NVS data in the system.
   * 
   * @param nvsNamespace The NVS namespace to use (default: "appcfg")
   *                     Must be a valid NVS namespace string (max 15 characters).
   *                     The namespace is stored but NVS is not mounted until first use.
   * 
   * @note The Arduino core must have initialized the NVS/Preferences system.
   *       This typically happens automatically on ESP32.
   */
  explicit NVSConfigBus(const char* nvsNamespace = "appcfg");

  /**
   * @brief Load configuration for a specific module
   * 
   * Attempts to load the JSON configuration document for the specified module
   * from NVS. This method now prefers MessagePack storage (if available) and falls
   * back to legacy JSON string storage for backward compatibility. If legacy
   * JSON is found, it will be automatically migrated to MessagePack format.
   * 
   * @param moduleId The unique identifier for the module (e.g., "pulsfan", "blecfg")
   *                 This string is used directly as the NVS key.
   * @param doc The JSON document to populate with loaded data.
   *            Must be pre-allocated with sufficient capacity.
   *            Will be cleared if loading fails.
   * 
   * @return true if configuration was successfully loaded and parsed
   * @return false if configuration is missing, corrupted, or invalid
   * 
   * @note The caller is responsible for applying default values when this returns false.
   *       The document is cleared on failure to ensure a clean state.
   *       This method uses an internal buffer for MessagePack operations. For better
   *       memory control, use loadModuleConfigMsgPack() with a caller-provided buffer.
   * 
   * @example
   * ```cpp
   * DynamicJsonDocument doc(1024);
   * if (configBus.loadModuleConfig("pulsfan", doc)) {
   *   int minHr = doc["heartRateMin"] | 120;  // Use loaded value or default
   * } else {
   *   // Apply defaults
   *   doc["heartRateMin"] = 120;
   *   doc["heartRateMax"] = 180;
   * }
   * ```
   */
  bool loadModuleConfig(const char* moduleId, DynamicJsonDocument& doc);

  /**
   * @brief Save configuration for a specific module
   * 
   * Serializes the provided JSON document to a string and stores it in NVS
   * under the specified moduleId key. The entire module's configuration is
   * stored as a single JSON blob, allowing flexible schema evolution.
   * 
   * This method now prefers MessagePack storage (more efficient) but maintains
   * backward compatibility with JSON string storage.
   * 
   * @param moduleId The unique identifier for the module
   * @param doc The JSON document to serialize and store
   * 
   * @return true if the configuration was successfully saved
   * @return false if serialization or storage failed
   * 
   * @warning Avoid calling this method in tight loops. It performs NVS write
   *          operations which have limited write endurance. Intended for
   *          configuration changes, not high-frequency logging.
   * 
   * @example
   * ```cpp
   * DynamicJsonDocument doc(1024);
   * doc["heartRateMin"] = 120;
   * doc["heartRateMax"] = 180;
   * doc["firmwareVersion"] = "1.0.0";
   * configBus.saveModuleConfig("pulsfan", doc);
   * ```
   */
  bool saveModuleConfig(const char* moduleId, const DynamicJsonDocument& doc);

  /**
   * @brief Save configuration using MessagePack format (recommended)
   * 
   * Serializes the provided JSON document to MessagePack binary format and stores it
   * in NVS using putBytes(). This method avoids String allocations and is
   * more memory-efficient than JSON string storage.
   * 
   * @param moduleId The unique identifier for the module
   * @param doc The JSON document to serialize and store
   * @param buf Pre-allocated buffer for MessagePack serialization (must be large enough)
   * @param bufSize Size of the buffer in bytes
   * 
   * @return true if the configuration was successfully saved
   * @return false if serialization or storage failed (buffer too small, etc.)
   * 
   * @note The buffer should be sized appropriately. A good rule of thumb is
   *       to allocate 2x the JSON document capacity, or use measureMsgPack()
   *       to determine the exact size needed.
   * 
   * @example
   * ```cpp
   * DynamicJsonDocument doc(1024);
   * doc["heartRateMin"] = 120;
   * doc["heartRateMax"] = 180;
   * 
   * uint8_t msgPackBuf[2048];  // Buffer for MessagePack serialization
   * if (configBus.saveModuleConfigMsgPack("pulsfan", doc, msgPackBuf, sizeof(msgPackBuf))) {
   *   Serial.println("Saved using MessagePack");
   * }
   * ```
   */
  bool saveModuleConfigMsgPack(const char* moduleId,
                               const JsonDocument& doc,
                               uint8_t* buf,
                               size_t bufSize);

  /**
   * @brief Load configuration using MessagePack format (recommended)
   * 
   * Attempts to load the configuration from MessagePack binary format. If MessagePack is not
   * present, falls back to legacy JSON string storage. This method avoids
   * String allocations in the MessagePack path.
   * 
   * @param moduleId The unique identifier for the module
   * @param doc The JSON document to populate with loaded data
   * @param buf Pre-allocated buffer for MessagePack deserialization
   * @param bufSize Size of the buffer in bytes
   * 
   * @return true if configuration was successfully loaded and parsed
   * @return false if configuration is missing, corrupted, or invalid
   * 
   * @note This method automatically migrates from JSON to MessagePack on first load
   *       if legacy JSON data is found and MessagePack is not present.
   * 
   * @example
   * ```cpp
   * DynamicJsonDocument doc(1024);
   * uint8_t msgPackBuf[2048];
   * if (configBus.loadModuleConfigMsgPack("pulsfan", doc, msgPackBuf, sizeof(msgPackBuf))) {
   *   Serial.println("Loaded using MessagePack");
   * } else {
   *   // Apply defaults
   *   doc["heartRateMin"] = 120;
   * }
   * ```
   */
  bool loadModuleConfigMsgPack(const char* moduleId,
                               DynamicJsonDocument& doc,
                               uint8_t* buf,
                               size_t bufSize);

  /**
   * @brief Clear configuration for a specific module
   * 
   * Removes the configuration data for the specified module from NVS.
   * Other modules' configurations are unaffected.
   * 
   * @param moduleId The unique identifier for the module to clear
   * 
   * @return true if the module existed and was successfully removed
   * @return false if the module did not exist or removal failed
   * 
   * @example
   * ```cpp
   * // Clear only the BLE configuration
   * configBus.clearModuleConfig("blecfg");
   * // Other modules like "pulsfan" remain intact
   * ```
   */
  bool clearModuleConfig(const char* moduleId);

  /**
   * @brief Clear all configuration data in the namespace
   * 
   * Removes all configuration data stored in this bus's NVS namespace.
   * This effectively performs a factory reset for all modules using this bus.
   * 
   * @return true if the operation succeeded
   * @return false if the operation failed
   * 
   * @warning This operation is irreversible and affects all modules using
   *          this NVSConfigBus instance. Use with caution.
   * 
   * @example
   * ```cpp
   * // Factory reset: clear all configuration
   * if (configBus.clearAll()) {
   *   // Reinitialize all modules with defaults
   *   initializeDefaultConfig();
   * }
   * ```
   */
  bool clearAll();

private:
  const char* _namespace;  ///< The NVS namespace for this bus instance
  
  /**
   * @brief Build the MessagePack key name from moduleId (uses :mp suffix)
   * 
   * @param moduleId The module identifier
   * @param keyBuf Output buffer for the key (must be at least strlen(moduleId) + 4)
   * @param keyBufSize Size of the key buffer
   * @return true if key was built successfully
   */
  bool buildMsgPackKey(const char* moduleId, char* keyBuf, size_t keyBufSize) const;
};

