#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cstdlib>

// Simple JSON parser (minimal implementation for this project)
// In production, would use nlohmann/json or similar

struct EdgeConfig {
    std::string to;
    std::string host;
    int port;
    std::string relationship;
    std::string team;
};

struct ChunkConfig {
    int default_chunk_size;
    int max_chunk_size;
    int min_chunk_size;
};

struct DataPartitioning {
    std::string strategy;
    std::vector<std::string> owned_dates;
};

struct ProcessConfig {
    std::string process_id;
    std::string role;
    std::string listen_host;
    int listen_port;
    std::string data_path;
    std::string team;
    bool is_team_leader;
    std::vector<EdgeConfig> edges;
    DataPartitioning data_partitioning;
    ChunkConfig chunk_config;
};

class ConfigParser {
public:
    static ProcessConfig loadConfig(const std::string& config_file) {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file: " + config_file);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        ProcessConfig config;

        // Simple manual JSON parsing (replace with proper library in production)
        config.process_id = extractString(content, "process_id");
        config.role = extractString(content, "role");
        config.listen_host = extractString(content, "listen_host");
        config.listen_port = extractInt(content, "listen_port");

        // Check for FIRE_DATA_PATH environment variable first, then fall back to config
        const char* env_data_path = std::getenv("FIRE_DATA_PATH");
        if (env_data_path != nullptr && std::string(env_data_path).length() > 0) {
            config.data_path = std::string(env_data_path);
            std::cout << "Using data path from FIRE_DATA_PATH env: " << config.data_path << std::endl;
        } else {
            config.data_path = extractString(content, "data_path");
        }

        config.team = extractString(content, "\"team\"");
        config.is_team_leader = extractBool(content, "is_team_leader");

        // Extract edges
        config.edges = extractEdges(content);

        // Extract data partitioning
        config.data_partitioning.strategy = extractString(content, "strategy");
        config.data_partitioning.owned_dates = extractStringArray(content, "owned_dates");

        // Extract chunk config
        config.chunk_config.default_chunk_size = extractInt(content, "default_chunk_size");
        config.chunk_config.max_chunk_size = extractInt(content, "max_chunk_size");
        config.chunk_config.min_chunk_size = extractInt(content, "min_chunk_size");

        return config;
    }

private:
    static std::string extractString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";

        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        if (json[pos] == 'n') return ""; // null value
        if (json[pos] != '"') return "";

        pos++; // skip opening quote
        size_t end = json.find('"', pos);
        return json.substr(pos, end - pos);
    }

    static int extractInt(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;

        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        size_t end = pos;
        while (end < json.length() && (isdigit(json[end]) || json[end] == '-')) end++;

        return std::stoi(json.substr(pos, end - pos));
    }

    static bool extractBool(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;

        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        return (json.substr(pos, 4) == "true");
    }

    static std::vector<std::string> extractStringArray(const std::string& json, const std::string& key) {
        std::vector<std::string> result;
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return result;

        pos = json.find('[', pos);
        if (pos == std::string::npos) return result;

        size_t end = json.find(']', pos);
        std::string arrayContent = json.substr(pos + 1, end - pos - 1);

        size_t current = 0;
        while (current < arrayContent.length()) {
            size_t quote1 = arrayContent.find('"', current);
            if (quote1 == std::string::npos) break;

            size_t quote2 = arrayContent.find('"', quote1 + 1);
            if (quote2 == std::string::npos) break;

            result.push_back(arrayContent.substr(quote1 + 1, quote2 - quote1 - 1));
            current = quote2 + 1;
        }

        return result;
    }

    static std::vector<EdgeConfig> extractEdges(const std::string& json) {
        std::vector<EdgeConfig> edges;

        size_t edgesStart = json.find("\"edges\":");
        if (edgesStart == std::string::npos) return edges;

        size_t arrayStart = json.find('[', edgesStart);
        if (arrayStart == std::string::npos) return edges;

        size_t arrayEnd = json.find(']', arrayStart);
        std::string edgesContent = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        // Find each edge object
        size_t pos = 0;
        while (pos < edgesContent.length()) {
            size_t objStart = edgesContent.find('{', pos);
            if (objStart == std::string::npos) break;

            size_t objEnd = edgesContent.find('}', objStart);
            if (objEnd == std::string::npos) break;

            std::string edgeJson = edgesContent.substr(objStart, objEnd - objStart + 1);

            EdgeConfig edge;
            edge.to = extractString(edgeJson, "to");
            edge.host = extractString(edgeJson, "host");
            edge.port = extractInt(edgeJson, "port");
            edge.relationship = extractString(edgeJson, "relationship");
            edge.team = extractString(edgeJson, "team");

            edges.push_back(edge);
            pos = objEnd + 1;
        }

        return edges;
    }
};

#endif // CONFIG_HPP
