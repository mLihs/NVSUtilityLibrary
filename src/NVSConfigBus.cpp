#include "NVSConfigBus.h"
#include <string.h>

NVSConfigBus::NVSConfigBus(const char* nvsNamespace)
    : _namespace(nvsNamespace) {
  // Constructor only stores the namespace; NVS is accessed on-demand
  // No initialization needed as Preferences handles NVS mounting automatically
}

bool NVSConfigBus::buildMsgPackKey(const char* moduleId, char* keyBuf, size_t keyBufSize) const {
  if (moduleId == nullptr || keyBuf == nullptr || keyBufSize == 0) {
    return false;
  }
  
  size_t moduleIdLen = strlen(moduleId);
  // Use suffix ":mp" (MessagePack) to stay within NVS 15-char limit
  const char* suffix = ":mp";  // 3 characters
  size_t suffixLen = 3;
  
  // Check if combined key would exceed NVS key limit (15 characters max)
  if (moduleIdLen + suffixLen > 15) {
    NVS_CFG_LOG("buildMsgPackKey: moduleId too long for NVS (max 12 chars with ':mp' suffix for MessagePack)");
    return false;
  }
  
  if (moduleIdLen + suffixLen + 1 > keyBufSize) {  // suffix + null terminator
    return false;
  }
  
  strncpy(keyBuf, moduleId, keyBufSize);
  keyBuf[moduleIdLen] = '\0';
  strncat(keyBuf, suffix, keyBufSize - moduleIdLen - 1);
  return true;
}

bool NVSConfigBus::loadModuleConfig(const char* moduleId, DynamicJsonDocument& doc) {
  if (moduleId == nullptr || strlen(moduleId) == 0) {
    NVS_CFG_LOG("loadModuleConfig: invalid moduleId");
    doc.clear();
    return false;
  }

  // Use internal buffer for MessagePack operations (default size: 2048 bytes)
  // This is a compromise - for better control, use loadModuleConfigMsgPack() with caller buffer
  const size_t internalBufSize = 2048;
  uint8_t* internalBuf = (uint8_t*)malloc(internalBufSize);
  if (internalBuf == nullptr) {
    NVS_CFG_LOG("loadModuleConfig: failed to allocate internal buffer, falling back to JSON");
    // Fall through to JSON-only path
  } else {
    // Try MessagePack first
    if (loadModuleConfigMsgPack(moduleId, doc, internalBuf, internalBufSize)) {
      free(internalBuf);
      return true;
    }
    free(internalBuf);
  }

  // Fallback to JSON bytes storage (or legacy JSON string)
  Preferences prefs;
  if (!prefs.begin(_namespace, true)) {  // Read-only mode
    NVS_CFG_LOG("loadModuleConfig: failed to open Preferences namespace");
    doc.clear();
    return false;
  }

  // Check if JSON key exists
  if (!prefs.isKey(moduleId)) {
    prefs.end();
    doc.clear();
    return false;
  }

  // Try to read as bytes first (new format)
  size_t jsonSize = prefs.getBytesLength(moduleId);
  bool migratedFromString = false;
  
  if (jsonSize > 0) {
    // JSON stored as bytes (new format)
    const size_t jsonBufSize = 2048;
    uint8_t* jsonBuf = (uint8_t*)malloc(jsonBufSize);
    if (jsonBuf == nullptr) {
      NVS_CFG_LOG("loadModuleConfig: failed to allocate JSON buffer");
      prefs.end();
      doc.clear();
      return false;
    }

    if (jsonSize > jsonBufSize) {
      NVS_CFG_LOG("loadModuleConfig: JSON buffer too small");
      prefs.end();
      free(jsonBuf);
      doc.clear();
      return false;
    }

    size_t bytesRead = prefs.getBytes(moduleId, jsonBuf, jsonSize);
    prefs.end();

    if (bytesRead != jsonSize) {
      NVS_CFG_LOG("loadModuleConfig: JSON read size mismatch");
      free(jsonBuf);
      doc.clear();
      return false;
    }

    // Deserialize JSON from bytes (before freeing buffer)
    DeserializationError error = deserializeJson(doc, jsonBuf, jsonSize);
    free(jsonBuf);

    if (error) {
      NVS_CFG_LOG("loadModuleConfig: JSON deserialization from bytes failed");
      doc.clear();
      return false;
    }
  } else {
    // Legacy: JSON stored as string - read once and migrate to bytes immediately
    // This is the ONLY place we use String, and only for one-time migration
    String jsonString = prefs.getString(moduleId, "");
    prefs.end();

    if (jsonString.length() == 0) {
      NVS_CFG_LOG("loadModuleConfig: empty JSON string read");
      doc.clear();
      return false;
    }

    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
      NVS_CFG_LOG("loadModuleConfig: JSON deserialization from string failed");
      doc.clear();
      return false;
    }

    // Immediately migrate legacy string to bytes format
    migratedFromString = true;
    const size_t migrateBufSize = 2048;
    uint8_t* migrateBuf = (uint8_t*)malloc(migrateBufSize);
    if (migrateBuf != nullptr) {
      // Serialize JSON to bytes buffer
      size_t jsonBytesSize = serializeJson(doc, migrateBuf, migrateBufSize);
      if (jsonBytesSize > 0 && jsonBytesSize < migrateBufSize) {
        Preferences prefsWrite;
        if (prefsWrite.begin(_namespace, false)) {
          // Remove old string key first
          prefsWrite.remove(moduleId);
          // Write as bytes (putBytes overwrites, so removing first ensures clean state)
          size_t bytesWritten = prefsWrite.putBytes(moduleId, migrateBuf, jsonBytesSize);
          prefsWrite.end();
          if (bytesWritten == jsonBytesSize) {
            NVS_CFG_LOG("loadModuleConfig: migrated JSON string to bytes");
          }
        }
      }
      free(migrateBuf);
    }
  }

  // Migration: If we loaded from JSON and MessagePack doesn't exist, migrate to MessagePack
  // This is a one-time migration that happens automatically
  char msgPackKey[64];
  if (buildMsgPackKey(moduleId, msgPackKey, sizeof(msgPackKey))) {
    Preferences prefsWrite;
    if (prefsWrite.begin(_namespace, false)) {  // Read-write mode
      if (!prefsWrite.isKey(msgPackKey)) {
        // MessagePack doesn't exist, migrate from JSON to MessagePack
        const size_t migrateBufSize = 2048;
        uint8_t* migrateBuf = (uint8_t*)malloc(migrateBufSize);
        if (migrateBuf != nullptr) {
          if (saveModuleConfigMsgPack(moduleId, doc, migrateBuf, migrateBufSize)) {
            NVS_CFG_LOG("loadModuleConfig: migrated JSON to MessagePack");
          }
          free(migrateBuf);
        }
      }
      prefsWrite.end();
    }
  }

  return true;
}

bool NVSConfigBus::saveModuleConfig(const char* moduleId, const DynamicJsonDocument& doc) {
  if (moduleId == nullptr || strlen(moduleId) == 0) {
    NVS_CFG_LOG("saveModuleConfig: invalid moduleId");
    return false;
  }

  // Try to save as MessagePack first (preferred method)
  const size_t internalBufSize = 2048;
  uint8_t* internalBuf = (uint8_t*)malloc(internalBufSize);
  if (internalBuf != nullptr) {
    if (saveModuleConfigMsgPack(moduleId, doc, internalBuf, internalBufSize)) {
      free(internalBuf);
      return true;
    }
    free(internalBuf);
  }

  // Fallback to JSON bytes storage if MessagePack fails
  const size_t jsonBufSize = 2048;
  uint8_t* jsonBuf = (uint8_t*)malloc(jsonBufSize);
  if (jsonBuf == nullptr) {
    NVS_CFG_LOG("saveModuleConfig: failed to allocate JSON buffer");
    return false;
  }

  // Serialize JSON document to bytes buffer
  size_t jsonSize = serializeJson(doc, jsonBuf, jsonBufSize);
  
  if (jsonSize == 0 || jsonSize >= jsonBufSize) {
    NVS_CFG_LOG("saveModuleConfig: JSON serialization failed or buffer too small");
    free(jsonBuf);
    return false;
  }

  // Write JSON bytes to NVS
  Preferences prefs;
  if (!prefs.begin(_namespace, false)) {  // Read-write mode
    NVS_CFG_LOG("saveModuleConfig: failed to open Preferences namespace");
    free(jsonBuf);
    return false;
  }

  size_t bytesWritten = prefs.putBytes(moduleId, jsonBuf, jsonSize);
  prefs.end();
  free(jsonBuf);

  if (bytesWritten == 0 || bytesWritten != jsonSize) {
    NVS_CFG_LOG("saveModuleConfig: JSON write failed");
    return false;
  }

  return true;
}

bool NVSConfigBus::saveModuleConfigMsgPack(const char* moduleId,
                                           const JsonDocument& doc,
                                           uint8_t* buf,
                                           size_t bufSize) {
  if (moduleId == nullptr || strlen(moduleId) == 0) {
    NVS_CFG_LOG("saveModuleConfigMsgPack: invalid moduleId");
    return false;
  }

  if (buf == nullptr || bufSize == 0) {
    NVS_CFG_LOG("saveModuleConfigMsgPack: invalid buffer");
    return false;
  }

  // Serialize to MessagePack (ArduinoJson 7 uses MessagePack format)
  // Note: serializeMsgPack returns 0 on error, or the number of bytes written
  size_t msgPackSize = serializeMsgPack(doc, buf, bufSize);
  
  if (msgPackSize == 0) {
    NVS_CFG_LOG("saveModuleConfigMsgPack: MessagePack serialization returned 0 (serialization failed)");
    return false;
  }
  
  if (msgPackSize > bufSize) {
    char errorMsg[128];
    snprintf(errorMsg, sizeof(errorMsg), "saveModuleConfigMsgPack: buffer too small (needed %zu, have %zu)", msgPackSize, bufSize);
    NVS_CFG_LOG(errorMsg);
    return false;
  }

  // Build MessagePack key (using :mp suffix)
  char msgPackKey[64];
  if (!buildMsgPackKey(moduleId, msgPackKey, sizeof(msgPackKey))) {
    NVS_CFG_LOG("saveModuleConfigMsgPack: failed to build MessagePack key");
    return false;
  }

  // Write to NVS using putBytes
  Preferences prefs;
  if (!prefs.begin(_namespace, false)) {  // Read-write mode
    NVS_CFG_LOG("saveModuleConfigMsgPack: failed to open Preferences namespace");
    return false;
  }

  size_t bytesWritten = prefs.putBytes(msgPackKey, buf, msgPackSize);
  prefs.end();

  if (bytesWritten == 0) {
    NVS_CFG_LOG("saveModuleConfigMsgPack: putBytes returned 0 (NVS might be full or key invalid)");
    return false;
  }
  
  if (bytesWritten != msgPackSize) {
    char errorMsg[128];
    snprintf(errorMsg, sizeof(errorMsg), "saveModuleConfigMsgPack: write size mismatch (expected %zu, got %zu)", msgPackSize, bytesWritten);
    NVS_CFG_LOG(errorMsg);
    return false;
  }

  return true;
}

bool NVSConfigBus::loadModuleConfigMsgPack(const char* moduleId,
                                           DynamicJsonDocument& doc,
                                           uint8_t* buf,
                                           size_t bufSize) {
  if (moduleId == nullptr || strlen(moduleId) == 0) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: invalid moduleId");
    doc.clear();
    return false;
  }

  if (buf == nullptr || bufSize == 0) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: invalid buffer");
    doc.clear();
    return false;
  }

  // Build MessagePack key (using :mp suffix)
  char msgPackKey[64];
  if (!buildMsgPackKey(moduleId, msgPackKey, sizeof(msgPackKey))) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: failed to build MessagePack key");
    doc.clear();
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(_namespace, true)) {  // Read-only mode
    NVS_CFG_LOG("loadModuleConfigMsgPack: failed to open Preferences namespace");
    doc.clear();
    return false;
  }

  // Check if MessagePack key exists
  if (!prefs.isKey(msgPackKey)) {
    prefs.end();
    doc.clear();
    return false;  // MessagePack not present, caller should try JSON fallback
  }

  // Get the size of stored MessagePack data
  // Note: getBytesLength() is available in ESP32 Arduino core
  size_t storedSize = prefs.getBytesLength(msgPackKey);
  if (storedSize == 0 || storedSize > bufSize) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: invalid stored size or buffer too small");
    prefs.end();
    doc.clear();
    return false;
  }

  // Read MessagePack data from NVS
  size_t bytesRead = prefs.getBytes(msgPackKey, buf, storedSize);
  prefs.end();

  if (bytesRead != storedSize) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: read size mismatch");
    doc.clear();
    return false;
  }

  // Deserialize MessagePack (ArduinoJson 7 uses MessagePack format)
  DeserializationError error = deserializeMsgPack(doc, buf, bytesRead);

  if (error) {
    NVS_CFG_LOG("loadModuleConfigMsgPack: MessagePack deserialization failed");
    doc.clear();
    return false;
  }

  return true;
}

bool NVSConfigBus::clearModuleConfig(const char* moduleId) {
  if (moduleId == nullptr || strlen(moduleId) == 0) {
    NVS_CFG_LOG("clearModuleConfig: invalid moduleId");
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(_namespace, false)) {  // Read-write mode
    NVS_CFG_LOG("clearModuleConfig: failed to open Preferences namespace");
    return false;
  }

  // Check if JSON key exists before removing
  bool jsonExisted = prefs.isKey(moduleId);
  if (jsonExisted) {
    prefs.remove(moduleId);
  }

  // Also remove MessagePack key if it exists
  char msgPackKey[64];
  bool msgPackExisted = false;
  if (buildMsgPackKey(moduleId, msgPackKey, sizeof(msgPackKey))) {
    msgPackExisted = prefs.isKey(msgPackKey);
    if (msgPackExisted) {
      prefs.remove(msgPackKey);
    }
  }
  
  prefs.end();
  return jsonExisted || msgPackExisted;  // Return true if either existed
}

bool NVSConfigBus::clearAll() {
  Preferences prefs;
  if (!prefs.begin(_namespace, false)) {  // Read-write mode
    NVS_CFG_LOG("clearAll: failed to open Preferences namespace");
    return false;
  }

  bool success = prefs.clear();
  prefs.end();
  
  if (!success) {
    NVS_CFG_LOG("clearAll: clear operation failed");
  }
  
  return success;
}

