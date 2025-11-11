#ifndef FIRE_DATA_LOADER_HPP
#define FIRE_DATA_LOADER_HPP

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// Fire record structure matching our protobuf (but for internal use)
struct FireDataRecord {
    double latitude;
    double longitude;
    std::string timestamp;
    std::string pollutant;
    double concentration;
    std::string unit;
    double raw_concentration;
    int aqi;
    int aqi_category;
    std::string site_name;
    std::string agency;
    std::string site_id;
    std::string full_site_id;
};

class FireDataLoader {
public:
    FireDataLoader(const std::string& data_path) : data_path_(data_path) {
        if (!fs::exists(data_path_)) {
            throw std::runtime_error("Data path does not exist: " + data_path_);
        }
    }

    // Load fire data for specific dates and optional filters
    std::vector<FireDataRecord> loadData(
        const std::vector<std::string>& dates,
        const std::string& pollutant_filter = "",
        double lat_min = -90.0, double lat_max = 90.0,
        double lon_min = -180.0, double lon_max = 180.0,
        int max_records = -1) {

        std::vector<FireDataRecord> results;

        for (const auto& date : dates) {
            std::string date_dir = data_path_ + "/" + date;
            if (!fs::exists(date_dir)) {
                std::cerr << "Warning: Date directory not found: " << date_dir << std::endl;
                continue;
            }

            // Load all CSV files for this date
            for (const auto& entry : fs::directory_iterator(date_dir)) {
                if (entry.path().extension() == ".csv") {
                    loadCSV(entry.path().string(), results, pollutant_filter,
                            lat_min, lat_max, lon_min, lon_max, max_records);

                    if (max_records > 0 && results.size() >= static_cast<size_t>(max_records)) {
                        results.resize(max_records);
                        return results;
                    }
                }
            }
        }

        return results;
    }

    // Get available dates in the data directory
    std::vector<std::string> getAvailableDates() {
        std::vector<std::string> dates;
        for (const auto& entry : fs::directory_iterator(data_path_)) {
            if (entry.is_directory()) {
                dates.push_back(entry.path().filename().string());
            }
        }
        std::sort(dates.begin(), dates.end());
        return dates;
    }

private:
    std::string data_path_;

    void loadCSV(const std::string& csv_path,
                 std::vector<FireDataRecord>& results,
                 const std::string& pollutant_filter,
                 double lat_min, double lat_max,
                 double lon_min, double lon_max,
                 int max_records) {

        std::ifstream file(csv_path);
        if (!file.is_open()) {
            std::cerr << "Warning: Failed to open CSV: " << csv_path << std::endl;
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (max_records > 0 && results.size() >= static_cast<size_t>(max_records)) {
                break;
            }

            FireDataRecord record = parseCSVLine(line);

            // Apply filters
            if (!pollutant_filter.empty() && record.pollutant != pollutant_filter) {
                continue;
            }

            if (record.latitude < lat_min || record.latitude > lat_max) {
                continue;
            }

            if (record.longitude < lon_min || record.longitude > lon_max) {
                continue;
            }

            results.push_back(record);
        }
    }

    FireDataRecord parseCSVLine(const std::string& line) {
        FireDataRecord record;
        std::vector<std::string> fields;

        // Simple CSV parser (handles quoted fields)
        bool in_quotes = false;
        std::string current_field;

        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                fields.push_back(current_field);
                current_field.clear();
            } else {
                current_field += c;
            }
        }
        fields.push_back(current_field);

        // Parse fields according to CSV structure
        // "lat","lon","timestamp","pollutant","concentration","unit","raw","aqi","category","site","agency","id","full_id"
        if (fields.size() >= 13) {
            try {
                record.latitude = std::stod(fields[0]);
                record.longitude = std::stod(fields[1]);
                record.timestamp = fields[2];
                record.pollutant = fields[3];
                record.concentration = std::stod(fields[4]);
                record.unit = fields[5];
                record.raw_concentration = std::stod(fields[6]);
                record.aqi = std::stoi(fields[7]);
                record.aqi_category = std::stoi(fields[8]);
                record.site_name = fields[9];
                record.agency = fields[10];
                record.site_id = fields[11];
                record.full_site_id = fields[12];
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to parse line: " << e.what() << std::endl;
                // Return empty/default record
            }
        }

        return record;
    }
};

#endif // FIRE_DATA_LOADER_HPP
