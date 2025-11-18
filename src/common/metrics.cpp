#include "metrics.hpp"

#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <thread>
#include <cstdlib>   // std::getenv
#include <optional>

#if defined(_WIN32)
  #include <windows.h>
  static int get_pid() { return static_cast<int>(GetCurrentProcessId()); }
  static std::string get_hostname() {
      char name[MAX_COMPUTERNAME_LENGTH + 1]; DWORD sz = MAX_COMPUTERNAME_LENGTH + 1;
      if (GetComputerNameA(name, &sz)) return std::string(name, sz);
      return "unknown-host";
  }
#else
  #include <unistd.h>
  static int get_pid() { return static_cast<int>(::getpid()); }
  static std::string get_hostname() {
      char buf[256] = {0};
      if (::gethostname(buf, sizeof(buf)) == 0) return std::string(buf);
      return "unknown-host";
  }
#endif

namespace {
    std::mutex g_mutex;
    std::ofstream g_ofs;
    std::string g_process_id;
    std::string g_role;
    std::string g_hostname;
    std::string g_path;
    bool g_initialized = false;
    long long g_start_ms = 0;

    enum class OpenPolicy { Append, Overwrite };

    // --- helpers ---
    static inline long long now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    std::string sanitize_fs(std::string s) {
        for (char& c : s) {
            if (c == '/' || c == '\\' || c == ' ' || c == ':' || c == '\n' || c == '\r' || c == '\t')
                c = '-';
        }
        return s;
    }

    std::string make_path(const std::string& dir,
                          const std::string& process_id,
                          const std::string& role,
                          const std::string& hostname,
                          bool unique_name,
                          long long start_ms,
                          int pid)
    {
        std::ostringstream oss;
        oss << dir << "/metrics-"
            << sanitize_fs(role) << "-"
            << sanitize_fs(process_id) << "-"
            << sanitize_fs(hostname);
        if (unique_name) {
            oss << "-" << pid << "-" << start_ms;
        }
        oss << ".csv";
        return oss.str();
    }

    // Basic CSV escaping: wrap in quotes if needed; double internal quotes
    std::string csv_escape(const std::string& in) {
        bool needs = false;
        for (char c : in) {
            if (c == '"' || c == ',' || c == '\n' || c == '\r') { needs = true; break; }
        }
        if (!needs) return in;
        std::string out; out.reserve(in.size() + 2);
        out.push_back('"');
        for (char c : in) {
            if (c == '"') out += "\"\"";
            else out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    bool file_is_empty(const std::string& path) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(path, ec);
        return ec ? true : (sz == 0);
    }

    void write_header_if_needed(const std::string& path, OpenPolicy policy) {
        if (!g_ofs.is_open()) return;
        // Overwrite always needs header; Append only if file empty
        if (policy == OpenPolicy::Overwrite || file_is_empty(path)) {
            g_ofs << "wall_ms,steady_ms,event,request_id,process,role,hostname,pid,thread_id,"
                     "queue_depth,active_count,chunk_number,records,extra\n";
            g_ofs.flush();
        }
    }

    // Read env var as bool: "1","true","yes" are true
    bool getenv_bool(const char* key, bool defv) {
        const char* v = std::getenv(key);
        if (!v) return defv;
        std::string s = v;
        for (auto& c : s) c = (char)tolower(c);
        return (s == "1" || s == "true" || s == "yes" || s == "y");
    }
}

namespace metrics {

void init(const std::string& log_path,
          const std::string& process_id,
          const std::string& role)
{
    std::lock_guard<std::mutex> lg(g_mutex);
    if (g_initialized) return;

    // Decide open policy & name policy from env
    // Defaults: append + (use provided path as-is)
    const bool overwrite = getenv_bool("METRICS_OVERWRITE", false);
    const OpenPolicy policy = overwrite ? OpenPolicy::Overwrite : OpenPolicy::Append;

    // Ensure parent directory exists
    try {
        std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
    } catch (...) {
        // ignore; open() may still succeed if dir already exists
    }

    std::ios::openmode mode = std::ios::out | (policy == OpenPolicy::Overwrite ? std::ios::trunc : std::ios::app);
    g_ofs.open(log_path, mode);
    if (!g_ofs.is_open()) {
        std::cerr << "[metrics] Failed to open log file: " << log_path << "\n";
        return;
    }

    g_process_id = process_id;
    g_role       = role;
    g_hostname   = get_hostname();
    g_start_ms   = now_ms();
    g_path       = log_path;

    write_header_if_needed(g_path, policy);
    g_initialized = true;
}

void init_with_dir(const std::string& log_dir,
                   const std::string& process_id,
                   const std::string& role)
{
    std::lock_guard<std::mutex> lg(g_mutex);
    if (g_initialized) return;

    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec); // ok if already exists

    const bool overwrite  = getenv_bool("METRICS_OVERWRITE", true);
    const bool unique_name = getenv_bool("METRICS_FILENAME_UNIQUE", false);
    const OpenPolicy policy = overwrite ? OpenPolicy::Overwrite : OpenPolicy::Append;

    g_process_id = process_id;
    g_role       = role;
    g_hostname   = get_hostname();
    g_start_ms   = now_ms();

    const int pid = get_pid();
    const std::string path = make_path(log_dir, process_id, role, g_hostname, unique_name, g_start_ms, pid);

    std::ios::openmode mode = std::ios::out | (policy == OpenPolicy::Overwrite ? std::ios::trunc : std::ios::app);
    g_ofs.open(path, mode);
    if (!g_ofs.is_open()) {
        std::cerr << "[metrics] Failed to open log file: " << path << "\n";
        return;
    }
    g_path = path;

    write_header_if_needed(g_path, policy);
    g_initialized = true;
}

bool is_enabled() {
    std::lock_guard<std::mutex> lg(g_mutex);
    return g_initialized && g_ofs.is_open();
}

void shutdown() {
    std::lock_guard<std::mutex> lg(g_mutex);
    if (g_ofs.is_open()) {
        g_ofs.flush();
        g_ofs.close();
    }
    g_initialized = false;
}

void log_event(const std::string& event,
               const std::string& request_id,
               int queue_depth,
               int active_count,
               int chunk_number,
               int records,
               const std::string& extra)
{
    // Capture timestamps & ids outside the lock to reduce contention
    using namespace std::chrono;
    const auto wall_ms   = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    const auto steady_ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();

    const int pid = get_pid();
    std::stringstream tidss; tidss << std::this_thread::get_id();
    const std::string thread_id = tidss.str();

    std::string extra_sanitized = extra;
    for (char &c : extra_sanitized) if (c == '\n' || c == '\r') c = ' ';

    std::lock_guard<std::mutex> lg(g_mutex);
    if (!g_initialized || !g_ofs.is_open()) return;

    g_ofs
        << wall_ms << ','
        << steady_ms << ','
        << csv_escape(event) << ','
        << csv_escape(request_id) << ','
        << csv_escape(g_process_id) << ','
        << csv_escape(g_role) << ','
        << csv_escape(g_hostname) << ','
        << pid << ','
        << csv_escape(thread_id) << ','
        << queue_depth << ','
        << active_count << ','
        << chunk_number << ','
        << records << ','
        << csv_escape(extra_sanitized)
        << '\n';

    g_ofs.flush();
}

} // namespace metrics
