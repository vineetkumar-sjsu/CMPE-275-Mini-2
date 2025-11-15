#include "metrics.hpp"

#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <thread>

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
  #include <limits.h>
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
    bool g_initialized = false;

    // Build "logs/metrics-role-process-host-pid-startms.csv"
    std::string sanitize_fs(std::string s) {
        for (char& c : s) {
            if (c == '/' || c == '\\' || c == ' ' || c == ':' || c == '\n' || c == '\r' || c == '\t')
                c = '-';
        }
        return s;
    }

    std::string now_ms_string() {
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        return std::to_string(ms);
    }

    std::string make_path(const std::string& dir,
                          const std::string& process_id,
                          const std::string& role,
                          const std::string& hostname) {
        std::ostringstream oss;
        oss << dir << "/metrics-"
            << sanitize_fs(role) << "-"
            << sanitize_fs(process_id) << "-"
            << sanitize_fs(hostname) << "-"
            << get_pid() << "-"
            << now_ms_string() << ".csv";
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

}

namespace metrics {

void init(const std::string& log_path,
          const std::string& process_id,
          const std::string& role) {
    std::lock_guard<std::mutex> lg(g_mutex);
    if (g_initialized) return;

    // Ensure parent directory exists
    try {
        std::filesystem::path p(log_path);
        std::filesystem::create_directories(p.parent_path());
    } catch (...) {
        // ignore; open() may still succeed if dir already exists
    }

    g_ofs.open(log_path, std::ios::out | std::ios::app);
    if (!g_ofs.is_open()) {
        // Visible warning to stderr so you notice during runs
        std::cerr << "[metrics] Failed to open log file: " << log_path << "\n";
        return;
    }

    g_process_id = process_id;
    g_role = role;
    g_hostname = get_hostname();

    // Since each process gets a unique filename (with pid and timestamp),
    // the file will always be new, so always write the header
    g_ofs << "wall_ms,steady_ms,event,request_id,process,role,hostname,pid,thread_id,"
             "queue_depth,active_count,chunk_number,records,extra\n";
    g_ofs.flush();
    
    g_initialized = true;
}

void init_with_dir(const std::string& log_dir,
                   const std::string& process_id,
                   const std::string& role) {
    // Build unique path then call init(path, ...)
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec); // ok if already exists
    const std::string host = get_hostname();
    const std::string path = make_path(log_dir, process_id, role, host);
    init(path, process_id, role);
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
               const std::string& extra) {

    // Capture timestamps & ids outside the lock to reduce contention
    using namespace std::chrono;
    const auto now_wall = system_clock::now();
    const auto wall_ms = duration_cast<milliseconds>(now_wall.time_since_epoch()).count();
    const auto now_steady = steady_clock::now();
    const auto steady_ms = duration_cast<milliseconds>(now_steady.time_since_epoch()).count();

    const int pid = get_pid();
    std::stringstream tidss; tidss << std::this_thread::get_id();
    const std::string thread_id = tidss.str();

    // Sanitize extra (remove newlines; csv_escape will quote)
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

    // Keep simple & safe for now; you can batch-flush later for perf.
    g_ofs.flush();
}

} // namespace metrics
