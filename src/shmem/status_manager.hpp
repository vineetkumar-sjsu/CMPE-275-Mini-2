#ifndef STATUS_MANAGER_HPP
#define STATUS_MANAGER_HPP

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <stdexcept>
#include <iostream>

// Shared memory key for status coordination
#define STATUS_SHM_KEY 2275

// Status data structure - ONLY for coordination, NOT for results!
struct ProcessStatus {
    char process_id[8];          // A, B, C, D, E, F
    bool is_healthy;
    int pending_requests;
    int active_workers;
    int completed_requests;
    long last_update_timestamp;  // Unix timestamp
    double cpu_usage;            // 0.0 - 1.0
    int queue_depth;
};

struct TeamStatus {
    char team_name[16];          // "green" or "pink"
    int total_pending_requests;
    int total_active_workers;
    int total_processes;
    ProcessStatus processes[3];  // Max 3 processes per team
};

struct SystemStatus {
    int version;                 // Increment on each update
    bool shutdown_requested;
    TeamStatus green_team;
    TeamStatus pink_team;
    long last_global_update;
};

#define STATUS_SHM_SIZE (size_t)sizeof(SystemStatus)

class StatusManager {
public:
    StatusManager(bool create = false) : shmid_(-1), status_(nullptr), is_creator_(create) {
        if (create) {
            // Create shared memory segment
            shmid_ = shmget(STATUS_SHM_KEY, STATUS_SHM_SIZE, IPC_CREAT | IPC_EXCL | 0666);
            if (shmid_ < 0) {
                // Maybe it already exists, try to attach
                shmid_ = shmget(STATUS_SHM_KEY, STATUS_SHM_SIZE, 0666);
                if (shmid_ < 0) {
                    throw std::runtime_error("Failed to create shared memory segment");
                }
                is_creator_ = false;
            }
        } else {
            // Attach to existing segment
            shmid_ = shmget(STATUS_SHM_KEY, STATUS_SHM_SIZE, 0666);
            if (shmid_ < 0) {
                throw std::runtime_error("Failed to attach to shared memory segment. Is the leader running?");
            }
        }

        // Attach to memory
        status_ = (SystemStatus*)shmat(shmid_, NULL, 0);
        if (status_ == (SystemStatus*)-1) {
            throw std::runtime_error("Failed to attach to shared memory");
        }

        // Initialize if creator
        if (is_creator_) {
            initializeStatus();
        }

        std::cout << "StatusManager: " << (is_creator_ ? "Created" : "Attached to")
                  << " shared memory segment" << std::endl;
    }

    ~StatusManager() {
        if (status_ != nullptr && status_ != (SystemStatus*)-1) {
            shmdt(status_);
        }

        if (is_creator_ && shmid_ >= 0) {
            shmctl(shmid_, IPC_RMID, NULL);
            std::cout << "StatusManager: Destroyed shared memory segment" << std::endl;
        }
    }

    // Update status for a specific process
    void updateProcessStatus(const std::string& process_id,
                            int pending_requests,
                            int active_workers,
                            int completed_requests,
                            double cpu_usage = 0.0) {
        if (!status_) return;

        status_->version++;
        status_->last_global_update = time(nullptr);

        // Find the process and update
        TeamStatus* team = nullptr;
        if (process_id == "A" || process_id == "B" || process_id == "C") {
            team = &status_->green_team;
        } else if (process_id == "D" || process_id == "E" || process_id == "F") {
            team = &status_->pink_team;
        } else {
            return;
        }

        // Find or create process entry
        ProcessStatus* proc = nullptr;
        for (int i = 0; i < 3; i++) {
            if (strcmp(team->processes[i].process_id, process_id.c_str()) == 0) {
                proc = &team->processes[i];
                break;
            }
            if (strlen(team->processes[i].process_id) == 0) {
                proc = &team->processes[i];
                strncpy(proc->process_id, process_id.c_str(), 7);
                proc->process_id[7] = '\0';
                team->total_processes++;
                break;
            }
        }

        if (proc) {
            proc->is_healthy = true;
            proc->pending_requests = pending_requests;
            proc->active_workers = active_workers;
            proc->completed_requests = completed_requests;
            proc->last_update_timestamp = time(nullptr);
            proc->cpu_usage = cpu_usage;
            proc->queue_depth = pending_requests;
        }

        // Update team totals
        updateTeamTotals(team);
    }

    // Get load for a specific team (for load balancing decisions)
    int getTeamLoad(const std::string& team_name) {
        if (!status_) return 0;

        TeamStatus* team = nullptr;
        if (team_name == "green") {
            team = &status_->green_team;
        } else if (team_name == "pink") {
            team = &status_->pink_team;
        } else {
            return 0;
        }

        return team->total_pending_requests;
    }

    // Get team with lowest load (for fairness)
    std::string getLeastLoadedTeam() {
        if (!status_) return "green";

        if (status_->green_team.total_pending_requests <= status_->pink_team.total_pending_requests) {
            return "green";
        } else {
            return "pink";
        }
    }

    // Check if system shutdown is requested
    bool isShutdownRequested() {
        return status_ ? status_->shutdown_requested : false;
    }

    // Request system shutdown
    void requestShutdown() {
        if (status_) {
            status_->shutdown_requested = true;
            status_->version++;
        }
    }

    // Print current status (for debugging)
    void printStatus() {
        if (!status_) return;

        std::cout << "\n=== System Status (v" << status_->version << ") ===" << std::endl;
        std::cout << "Green Team: " << status_->green_team.total_pending_requests
                  << " pending, " << status_->green_team.total_active_workers << " active workers" << std::endl;
        std::cout << "Pink Team: " << status_->pink_team.total_pending_requests
                  << " pending, " << status_->pink_team.total_active_workers << " active workers" << std::endl;
        std::cout << "==============================\n" << std::endl;
    }

private:
    int shmid_;
    SystemStatus* status_;
    bool is_creator_;

    void initializeStatus() {
        if (!status_) return;

        memset(status_, 0, STATUS_SHM_SIZE);
        status_->version = 0;
        status_->shutdown_requested = false;

        strncpy(status_->green_team.team_name, "green", 15);
        status_->green_team.team_name[15] = '\0';

        strncpy(status_->pink_team.team_name, "pink", 15);
        status_->pink_team.team_name[15] = '\0';

        status_->last_global_update = time(nullptr);
    }

    void updateTeamTotals(TeamStatus* team) {
        if (!team) return;

        team->total_pending_requests = 0;
        team->total_active_workers = 0;

        for (int i = 0; i < 3; i++) {
            if (strlen(team->processes[i].process_id) > 0) {
                team->total_pending_requests += team->processes[i].pending_requests;
                team->total_active_workers += team->processes[i].active_workers;
            }
        }
    }
};

#endif // STATUS_MANAGER_HPP
