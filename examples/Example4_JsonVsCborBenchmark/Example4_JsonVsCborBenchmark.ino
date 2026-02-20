/**
 * @file Example4_JsonVsCborBenchmark.ino
 * @brief Benchmark example comparing JSON vs MessagePack storage performance and heap usage
 * 
 * This example demonstrates:
 * - Performance comparison between JSON string and MessagePack binary storage
 *   (ArduinoJson 7 uses MessagePack as the binary format)
 * - Heap memory usage and fragmentation analysis
 * - Storage size comparison
 * - Migration from JSON to MessagePack
 * 
 * The benchmark performs 100 save/load cycles for both formats, runs 10 times,
 * and reports aggregated statistics:
 * - Average execution time per operation
 * - Min/Max times
 * - Heap usage before/after
 * - Maximum allocatable heap (fragmentation indicator)
 * - Storage size in NVS
 */

#include <NVSConfigBus.h>
#include <ArduinoJson.h>

// Create configuration bus instances for testing
NVSConfigBus jsonBus("jsonbench");
NVSConfigBus msgPackBus("mpbench");

// Benchmark configuration
const int BENCHMARK_RUNS = 10;
const int ITERATIONS_PER_RUN = 100;

// Results storage
struct BenchmarkResults {
  unsigned long times[BENCHMARK_RUNS];
  unsigned long minTime;
  unsigned long maxTime;
  unsigned long avgTime;
  size_t storageSize;
  uint32_t heapBefore;
  uint32_t heapAfter;
  uint32_t maxAllocBefore;
  uint32_t maxAllocAfter;
};

BenchmarkResults jsonResults;
BenchmarkResults msgPackResults;

void populateConfig(DynamicJsonDocument& doc, int iteration) {
  doc["heartRateMin"] = 120 + (iteration % 50);
  doc["heartRateMax"] = 180 + (iteration % 50);
  doc["fanSpeed"] = 0.5f + (iteration % 100) / 200.0f;
  doc["enabled"] = (iteration % 2) == 0;
  doc["firmwareVersion"] = "1." + String(iteration % 10) + ".0";
  
  JsonArray tokens = doc["tokens"].to<JsonArray>();
  tokens.clear();
  for (int i = 0; i < 5; i++) {
    tokens.add("token_" + String(iteration * 10 + i));
  }
}

void printHeapStats(const char* label) {
  Serial.print(label);
  Serial.print(" - Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" bytes, Max Alloc: ");
  Serial.print(ESP.getMaxAllocHeap());
  Serial.println(" bytes");
  Serial.flush();
}

// Helper function to save JSON as bytes directly (bypassing MessagePack preference)
bool saveJsonDirect(const char* ns, const char* moduleId, const DynamicJsonDocument& doc) {
  Preferences prefs;
  if (!prefs.begin(ns, false)) {
    return false;
  }
  
  // Serialize JSON to bytes buffer
  uint8_t jsonBuf[4096];
  size_t jsonSize = serializeJson(doc, jsonBuf, sizeof(jsonBuf));
  
  if (jsonSize == 0 || jsonSize >= sizeof(jsonBuf)) {
    prefs.end();
    return false;
  }
  
  size_t bytesWritten = prefs.putBytes(moduleId, jsonBuf, jsonSize);
  prefs.end();
  
  return bytesWritten == jsonSize;
}

// Helper function to load JSON bytes directly (bypassing MessagePack preference)
bool loadJsonDirect(const char* ns, const char* moduleId, DynamicJsonDocument& doc) {
  Preferences prefs;
  if (!prefs.begin(ns, true)) {
    doc.clear();
    return false;
  }
  
  if (!prefs.isKey(moduleId)) {
    prefs.end();
    doc.clear();
    return false;
  }
  
  // Try to read as bytes first
  size_t jsonSize = prefs.getBytesLength(moduleId);
  
  if (jsonSize > 0) {
    // JSON stored as bytes
    uint8_t jsonBuf[4096];
    if (jsonSize > sizeof(jsonBuf)) {
      prefs.end();
      doc.clear();
      return false;
    }
    
    size_t bytesRead = prefs.getBytes(moduleId, jsonBuf, jsonSize);
    prefs.end();
    
    if (bytesRead != jsonSize) {
      doc.clear();
      return false;
    }
    
    DeserializationError error = deserializeJson(doc, jsonBuf, jsonSize);
    if (error) {
      doc.clear();
      return false;
    }
    return true;
  } else {
    // Legacy: JSON stored as string (fallback for migration)
    String jsonString = prefs.getString(moduleId, "");
    prefs.end();
    
    if (jsonString.length() == 0) {
      doc.clear();
      return false;
    }
    
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
      doc.clear();
      return false;
    }
    return true;
  }
}

unsigned long runJsonBenchmark(int iterations) {
  DynamicJsonDocument doc(2048);
  const char* moduleId = "testmod";  // Short name to fit NVS key limit
  
  unsigned long startTime = millis();
  
  for (int i = 0; i < iterations; i++) {
    populateConfig(doc, i);
    
    // Use direct JSON save (bypasses MessagePack preference in saveModuleConfig)
    if (!saveJsonDirect("jsonbench", moduleId, doc)) {
      Serial.print("ERROR: JSON save failed at iteration ");
      Serial.println(i);
      return 0;
    }
    
    doc.clear();
    // Use direct JSON load (bypasses MessagePack preference in loadModuleConfig)
    if (!loadJsonDirect("jsonbench", moduleId, doc)) {
      Serial.print("ERROR: JSON load failed at iteration ");
      Serial.println(i);
      return 0;
    }
  }
  
  return millis() - startTime;
}

unsigned long runMsgPackBenchmark(int iterations) {
  DynamicJsonDocument doc(2048);
  const char* moduleId = "testmod";  // Short name to fit NVS key limit
  uint8_t msgPackBuf[4096];
  
  unsigned long startTime = millis();
  
  for (int i = 0; i < iterations; i++) {
    populateConfig(doc, i);
    
    if (!msgPackBus.saveModuleConfigMsgPack(moduleId, doc, msgPackBuf, sizeof(msgPackBuf))) {
      Serial.print("ERROR: MessagePack save failed at iteration ");
      Serial.println(i);
      return 0;
    }
    
    doc.clear();
    if (!msgPackBus.loadModuleConfigMsgPack(moduleId, doc, msgPackBuf, sizeof(msgPackBuf))) {
      Serial.print("ERROR: MessagePack load failed at iteration ");
      Serial.println(i);
      return 0;
    }
  }
  
  return millis() - startTime;
}

void calculateStats(BenchmarkResults& results) {
  results.minTime = results.times[0];
  results.maxTime = results.times[0];
  unsigned long totalTime = 0;
  
  for (int i = 0; i < BENCHMARK_RUNS; i++) {
    totalTime += results.times[i];
    if (results.times[i] < results.minTime) results.minTime = results.times[i];
    if (results.times[i] > results.maxTime) results.maxTime = results.times[i];
  }
  
  results.avgTime = totalTime / BENCHMARK_RUNS;
}

void printResults(const char* name, BenchmarkResults& results) {
  Serial.println();
  Serial.print("=== ");
  Serial.print(name);
  Serial.println(" Results ===");
  
  Serial.println("Individual run times (ms):");
  for (int i = 0; i < BENCHMARK_RUNS; i++) {
    Serial.print("  Run ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(results.times[i]);
    Serial.println(" ms");
  }
  
  Serial.println();
  Serial.println("Statistics:");
  Serial.print("  Min time:     ");
  Serial.print(results.minTime);
  Serial.println(" ms");
  Serial.print("  Max time:     ");
  Serial.print(results.maxTime);
  Serial.println(" ms");
  Serial.print("  Avg time:     ");
  Serial.print(results.avgTime);
  Serial.println(" ms");
  Serial.print("  Avg per cycle: ");
  Serial.print(results.avgTime / (float)ITERATIONS_PER_RUN);
  Serial.println(" ms");
  
  Serial.println();
  Serial.println("Heap:");
  Serial.print("  Before: ");
  Serial.print(results.heapBefore);
  Serial.print(" bytes (Max Alloc: ");
  Serial.print(results.maxAllocBefore);
  Serial.println(" bytes)");
  Serial.print("  After:  ");
  Serial.print(results.heapAfter);
  Serial.print(" bytes (Max Alloc: ");
  Serial.print(results.maxAllocAfter);
  Serial.println(" bytes)");
  
  Serial.print("  Storage size: ");
  Serial.print(results.storageSize);
  Serial.println(" bytes");
}

void runFullBenchmark() {
  Serial.println("\n========================================");
  Serial.println("Running JSON Benchmark (10 runs)");
  Serial.println("========================================");
  
  // Clear existing data
  jsonBus.clearAll();
  
  // Record heap before
  jsonResults.heapBefore = ESP.getFreeHeap();
  jsonResults.maxAllocBefore = ESP.getMaxAllocHeap();
  
  // Run JSON benchmark 10 times
  for (int run = 0; run < BENCHMARK_RUNS; run++) {
    Serial.print("Run ");
    Serial.print(run + 1);
    Serial.print("/");
    Serial.print(BENCHMARK_RUNS);
    Serial.print("... ");
    
    jsonResults.times[run] = runJsonBenchmark(ITERATIONS_PER_RUN);
    
    Serial.print(jsonResults.times[run]);
    Serial.println(" ms");
    
    delay(100);  // Small delay between runs
  }
  
  // Record heap after
  jsonResults.heapAfter = ESP.getFreeHeap();
  jsonResults.maxAllocAfter = ESP.getMaxAllocHeap();
  
  // Get storage size for JSON (use getBytesLength for bytes, or getString for legacy)
  Preferences prefs;
  if (prefs.begin("jsonbench", true)) {
    if (prefs.isKey("testmod")) {
      size_t jsonSize = prefs.getBytesLength("testmod");
    if (jsonSize > 0) {
      // JSON stored as bytes
      jsonResults.storageSize = jsonSize;
    } else {
      // Legacy: JSON stored as string (should not happen in new code)
      // Use getBytesLength returns 0 for strings, so try reading as string for measurement
      String jsonString = prefs.getString("testmod", "");
      jsonResults.storageSize = jsonString.length();
      Serial.print("  (Warning: Found legacy string format, size: ");
      Serial.print(jsonResults.storageSize);
      Serial.println(" bytes)");
    }
      Serial.print("JSON key 'testmod' found, size: ");
      Serial.print(jsonResults.storageSize);
      Serial.println(" bytes");
    } else {
      Serial.println("WARNING: JSON key 'testmod' not found!");
      jsonResults.storageSize = 0;
    }
    prefs.end();
  } else {
    Serial.println("WARNING: Failed to open jsonbench namespace!");
  }
  
  calculateStats(jsonResults);
  
  Serial.println("\n========================================");
  Serial.println("Running MessagePack Benchmark (10 runs)");
  Serial.println("========================================");
  
  // Clear existing data
  msgPackBus.clearAll();
  
  // Record heap before
  msgPackResults.heapBefore = ESP.getFreeHeap();
  msgPackResults.maxAllocBefore = ESP.getMaxAllocHeap();
  
  // Run MessagePack benchmark 10 times
  for (int run = 0; run < BENCHMARK_RUNS; run++) {
    Serial.print("Run ");
    Serial.print(run + 1);
    Serial.print("/");
    Serial.print(BENCHMARK_RUNS);
    Serial.print("... ");
    
    msgPackResults.times[run] = runMsgPackBenchmark(ITERATIONS_PER_RUN);
    
    Serial.print(msgPackResults.times[run]);
    Serial.println(" ms");
    
    delay(100);  // Small delay between runs
  }
  
  // Record heap after
  msgPackResults.heapAfter = ESP.getFreeHeap();
  msgPackResults.maxAllocAfter = ESP.getMaxAllocHeap();
  
  // Get storage size for MessagePack (use getBytesLength for binary data)
  if (prefs.begin("mpbench", true)) {
    if (prefs.isKey("testmod:mp")) {
      msgPackResults.storageSize = prefs.getBytesLength("testmod:mp");
      Serial.print("MessagePack key 'testmod:mp' found, size: ");
      Serial.print(msgPackResults.storageSize);
      Serial.println(" bytes");
    } else {
      Serial.println("WARNING: MessagePack key 'testmod:mp' not found!");
      msgPackResults.storageSize = 0;
    }
    prefs.end();
  } else {
    Serial.println("WARNING: Failed to open mpbench namespace!");
  }
  
  calculateStats(msgPackResults);
}

void printComparison() {
  Serial.println("\n========================================");
  Serial.println("        BENCHMARK COMPARISON");
  Serial.println("========================================");
  
  printResults("JSON", jsonResults);
  printResults("MessagePack", msgPackResults);
  
  Serial.println("\n========================================");
  Serial.println("        SUMMARY");
  Serial.println("========================================");
  
  Serial.println();
  
  // Speed comparison (protect against division by zero)
  if (msgPackResults.avgTime > 0 && jsonResults.avgTime > 0) {
    float speedup = (float)jsonResults.avgTime / (float)msgPackResults.avgTime;
    Serial.print("MessagePack is ");
    if (speedup > 1.0f) {
      Serial.print(speedup, 2);
      Serial.println("x FASTER than JSON");
    } else {
      Serial.print(1.0f / speedup, 2);
      Serial.println("x SLOWER than JSON");
    }
  } else {
    Serial.println("Speed comparison: Unable to calculate (invalid times)");
  }
  
  // Storage comparison (protect against division by zero)
  if (jsonResults.storageSize > 0 && msgPackResults.storageSize > 0) {
    float sizeSavings = 100.0f * (1.0f - (float)msgPackResults.storageSize / (float)jsonResults.storageSize);
    Serial.print("Storage savings: ");
    Serial.print(sizeSavings, 1);
    Serial.print("% (JSON: ");
    Serial.print(jsonResults.storageSize);
    Serial.print(" bytes -> MessagePack: ");
    Serial.print(msgPackResults.storageSize);
    Serial.println(" bytes)");
  } else {
    Serial.println("Storage comparison: Unable to calculate");
    Serial.print("  JSON storage size: ");
    Serial.print(jsonResults.storageSize);
    Serial.println(" bytes");
    Serial.print("  MessagePack storage size: ");
    Serial.print(msgPackResults.storageSize);
    Serial.println(" bytes");
  }
  
  Serial.println();
  Serial.println("Key Observations:");
  Serial.println("- MessagePack typically uses 20-30% less storage than JSON");
  Serial.println("- MessagePack avoids String allocations (better heap stability)");
  Serial.println("- Max allocatable heap should remain stable with MessagePack");
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial monitor
  
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("NVS Config Bus - JSON vs MessagePack");
  Serial.println("        Extended Benchmark");
  Serial.println("========================================");
  Serial.print("Runs: ");
  Serial.print(BENCHMARK_RUNS);
  Serial.print(" x ");
  Serial.print(ITERATIONS_PER_RUN);
  Serial.println(" iterations each");
  Serial.println();
  
  printHeapStats("Initial");
  
  // Run the full benchmark
  runFullBenchmark();
  
  // Print comparison
  printComparison();
  
  Serial.println("\n========================================");
  Serial.println("Benchmark Complete");
  Serial.println("========================================");
}

void loop() {
  // Nothing to do in loop
  delay(10000);
}
