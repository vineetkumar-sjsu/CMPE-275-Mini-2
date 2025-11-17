#ifndef METRICS_HPP
#define METRICS_HPP

#include <string>

namespace metrics {

    // Initialize metrics logging to a specific path (file is created/append).
    // Safe to call multiple times; first success "wins".
    void init(const std::string& log_path,
              const std::string& process_id,
              const std::string& role);

    // Initialize by directory; builds a unique file name per process:
    // logs/metrics-<role>-<process>-<hostname>-<pid>-<startms>.csv
    void init_with_dir(const std::string& log_dir,
                       const std::string& process_id,
                       const std::string& role);

    // True if file is open and ready.
    bool is_enabled();

    // Flush and close the file (call on clean shutdown).
    void shutdown();

    // Log a single CSV event. All numeric fields optional (use -1 for unknown).
    void log_event(const std::string& event,
                   const std::string& request_id = "",
                   int queue_depth = -1,
                   int active_count = -1,
                   int chunk_number = -1,
                   int records = -1,
                   const std::string& extra = "");
}

#endif // METRICS_HPP
