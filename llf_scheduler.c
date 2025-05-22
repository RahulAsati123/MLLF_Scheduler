#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h> // For INT_MAX, INT_MIN
#include <math.h>   // For fabs, ceil
#include <stdbool.h> // For bool type

// --- Constants ---
#define MAX_TASKS 10
#define MAX_JOBS 1000
#define MAX_FILENAME_LEN 100
#define MAX_RESPONSE_TIMES_PER_TASK (MAX_JOBS / 2)
#define NO_TASK_FOUND -1 // Indicate no suitable Tmin found

// --- Data Structures ---
typedef struct {
    int id; int arrival_time; int period; int wcet; int deadline;
} Task;

typedef struct {
    int job_id; int task_id; int instance_number; int arrival_time;
    int wcet; int aet; int remaining_wcet; int remaining_aet;
    int absolute_deadline;
    int calculated_laxity; // Store calculated laxity for decisions
    int first_start_time;
    int last_start_time; int finish_time;
    enum { NOT_ARRIVED, READY, RUNNING, COMPLETED, MISSED } status;
    int response_time; int turnaround_time; int waiting_time;
} Job;

// Simulation state (dynamic parts) - passed to simulation steps
typedef struct {
    Job* ready_queue[MAX_JOBS];
    int ready_queue_size;
    Job* running_job;
    int current_time;
    int last_running_job_id;
    // ---- MLLF Specific ----
    int current_job_quantum_remaining; // How much longer the current job can run uninterrupted
    // -----------------------
    // Pointers to overall results updated during simulation
    int* context_switches_ptr;
    int* deadline_misses_ptr;
    int* completed_jobs_ptr;
    int* idle_time_ptr;
} SimulationState;

// --- Function Prototypes ---
long long gcd(long long a, long long b);
long long lcm(long long a, long long b);

// Core functionality functions
int read_tasks(const char* filename, Task tasks_arr[], int* task_count);
long long calculate_hyperperiod(const Task tasks_arr[], int task_count);
int generate_jobs(long long hyperperiod, const Task tasks_arr[], int task_count, Job jobs_arr[], int* job_count);
int read_actual_execution_times(const char* filename, Job jobs_arr[], int job_count);
// *** Changed function name ***
void run_mllf_simulation(int hyperperiod, Job jobs_arr[], int job_count, FILE* outfile,
                         int* context_switches, int* deadline_misses, int* completed_jobs, int* idle_time);
void analyze_schedule_results(const Job jobs_arr[], int job_count, const Task tasks_arr[], int task_count,
                              int context_switches, int deadline_misses, int completed_jobs, int idle_time,
                              int hyperperiod, FILE* outfile);

// Internal simulation helpers
void add_job_to_ready_queue(SimulationState* state, Job* job);
void remove_job_from_ready_queue(SimulationState* state, Job* job);
// *** Changed function name and logic ***
Job* select_mllf_task_Ta(SimulationState* state);
// *** New helper functions ***
int find_earliest_deadline_higher_laxity_job_deadline(SimulationState* state, Job* task_Ta, Job jobs_arr[], int job_count);
int calculate_mllf_quantum(SimulationState* state, Job* task_Ta, Job jobs_arr[], int job_count);
void calculate_all_laxities(SimulationState* state); // Helper to update laxity

void handle_arrivals(SimulationState* state, Job jobs_arr[], int job_count);
void handle_completion(SimulationState* state);
// *** Changed function name and logic ***
void make_mllf_scheduling_decision(SimulationState* state, Job* candidate_Ta, char* event_log, size_t log_size, Job jobs_arr[], int job_count);
void execute_running_job(SimulationState* state);
void check_deadline_misses(SimulationState* state, Job jobs_arr[], int job_count, FILE* outfile);


// --- Main Function ---
int main(int argc, char *argv[]) {
    char task_filename[MAX_FILENAME_LEN];
    char aet_filename[MAX_FILENAME_LEN];
    char output_filename[MAX_FILENAME_LEN];

    // Static allocation for simplicity, ensure MAX_TASKS/MAX_JOBS are sufficient
    Task tasks_list[MAX_TASKS];
    Job jobs_list[MAX_JOBS];
    int task_count = 0;
    int job_count = 0;

    // --- Get Filenames ---
    if (argc == 4) { /* Handle command line args */ /* ... */
        strncpy(task_filename, argv[1], MAX_FILENAME_LEN - 1); task_filename[MAX_FILENAME_LEN - 1] = '\0';
        strncpy(aet_filename, argv[2], MAX_FILENAME_LEN - 1); aet_filename[MAX_FILENAME_LEN - 1] = '\0';
        strncpy(output_filename, argv[3], MAX_FILENAME_LEN - 1); output_filename[MAX_FILENAME_LEN - 1] = '\0';
    } else { /* Prompt for filenames */ /* ... */
        printf("Enter task set filename: "); if (!fgets(task_filename, sizeof(task_filename), stdin)) return 1; task_filename[strcspn(task_filename, "\n")] = 0;
        printf("Enter AET filename: "); if (!fgets(aet_filename, sizeof(aet_filename), stdin)) return 1; aet_filename[strcspn(aet_filename, "\n")] = 0;
        printf("Enter output filename: "); if (!fgets(output_filename, sizeof(output_filename), stdin)) return 1; output_filename[strcspn(output_filename, "\n")] = 0;
    }

    // --- Setup ---
    if (!read_tasks(task_filename, tasks_list, &task_count)) return 1;
    if (task_count == 0) { printf("No tasks loaded.\n"); return 0; }

    long long hyperperiod_ll = calculate_hyperperiod(tasks_list, task_count);
    if (hyperperiod_ll <= 0 || hyperperiod_ll == -2) { fprintf(stderr, "Error: Invalid or excessive hyperperiod (%lld).\n", hyperperiod_ll); return 1; }
    if (hyperperiod_ll > INT_MAX) { fprintf(stderr, "Error: Hyperperiod exceeds INT_MAX.\n"); return 1; }
    int hyperperiod = (int)hyperperiod_ll;

    if (!generate_jobs(hyperperiod, tasks_list, task_count, jobs_list, &job_count)) return 1;
    if (job_count == 0) { printf("No jobs generated within hyperperiod.\n"); return 0; }

     // Initialize calculated_laxity
    for (int i = 0; i < job_count; ++i) {
        jobs_list[i].calculated_laxity = INT_MAX; // Initialize
    }

    if (!read_actual_execution_times(aet_filename, jobs_list, job_count)) return 1;

    // --- Open Output File ---
    FILE *outfile = fopen(output_filename, "w");
    if (!outfile) { perror("Error opening output file"); return 1; }
    printf("Output will be written to %s\n", output_filename);

    // --- Run Simulation & Analysis ---
    int context_switches = 0, deadline_misses = 0, completed_jobs = 0, idle_time = 0;

    // *** Call MLLF simulation ***
    run_mllf_simulation(hyperperiod, jobs_list, job_count, outfile,
                        &context_switches, &deadline_misses, &completed_jobs, &idle_time);

    analyze_schedule_results(jobs_list, job_count, tasks_list, task_count,
                             context_switches, deadline_misses, completed_jobs, idle_time,
                             hyperperiod, outfile);

    // --- Cleanup ---
    fclose(outfile);
    printf("Simulation finished. Results saved to %s\n", output_filename);

    return 0;
}


// --- Helper Function Implementations ---
long long gcd(long long a, long long b) {
    if (a < 0) a = -a; if (b < 0) b = -b;
    while (b) { a %= b; long long temp = a; a = b; b = temp; }
    return a == 0 ? 1 : a;
}

long long lcm(long long a, long long b) {
    if (a <= 0 || b <= 0) return 0;
    long long common_divisor = gcd(a, b);
    // Check potential overflow FIRST
    if (b > 0 && a > LLONG_MAX / (b / common_divisor)) {
        fprintf(stderr, "Warning: Potential overflow calculating LCM(%lld, %lld).\n", a, b);
        return -1; // Indicate overflow
    }
    return (a / common_divisor) * b;
}


// --- Core Function Implementations ---

int read_tasks(const char* filename, Task tasks_arr[], int* task_count) {
    FILE* file = fopen(filename, "r");
    if (!file) { perror("Error opening task file"); return 0; }
    *task_count = 0; int line_num = 0; int read_result;
    printf("Reading tasks from %s...\n", filename);
    while (*task_count < MAX_TASKS) {
        line_num++;
        read_result = fscanf(file, "%d %d %d %d",
                             &tasks_arr[*task_count].arrival_time, &tasks_arr[*task_count].period,
                             &tasks_arr[*task_count].wcet, &tasks_arr[*task_count].deadline);
        if (read_result == EOF) break;
        if (read_result != 4) { fprintf(stderr, "Error: Invalid task format line %d in %s.\n", line_num, filename); fclose(file); return 0; }
        tasks_arr[*task_count].id = *task_count;
        // Validation
        if (tasks_arr[*task_count].period <= 0 || tasks_arr[*task_count].wcet <= 0 || tasks_arr[*task_count].deadline <= 0 || tasks_arr[*task_count].arrival_time < 0) {
            fprintf(stderr, "Error: Task %d line %d: Non-positive P/WCET/D or negative A.\n", *task_count, line_num); fclose(file); return 0;
        }
         if (tasks_arr[*task_count].wcet > tasks_arr[*task_count].deadline) {
             fprintf(stderr, "Warning: Task %d line %d: WCET (%d) > Deadline (%d).\n", *task_count, line_num, tasks_arr[*task_count].wcet, tasks_arr[*task_count].deadline);
         }
        (*task_count)++;
    }
    // Check MAX_TASKS reached
    if (*task_count == MAX_TASKS) {
        int check; if (fscanf(file, "%d", &check) != EOF) fprintf(stderr, "Warning: MAX_TASKS (%d) reached...\n", MAX_TASKS);
    }
    if (*task_count == 0) { fprintf(stderr, "Error: No valid tasks found in %s.\n", filename); fclose(file); return 0; }
    fclose(file);
    printf("Successfully read %d tasks.\n", *task_count);
    return 1; // Success
}

long long calculate_hyperperiod(const Task tasks_arr[], int task_count) {
    if (task_count <= 0) return 0;
    if (task_count == 1) return tasks_arr[0].period > 0 ? tasks_arr[0].period : 0;
    long long result = tasks_arr[0].period;
    if (result <= 0) { fprintf(stderr, "Error: Task 0 has non-positive period.\n"); return 0; }
    for (int i = 1; i < task_count; i++) {
        if (tasks_arr[i].period <= 0) { fprintf(stderr, "Error: Task %d has non-positive period.\n", i); return 0; }
        result = lcm(result, tasks_arr[i].period);
        if (result == -1) { fprintf(stderr, "Error: Overflow calculating LCM.\n"); return -1;}
        if (result > (long long)INT_MAX * 4) { // Increased check threshold slightly
             fprintf(stderr, "Warning: Hyperperiod possibly excessive (> %lld).\n", (long long)INT_MAX * 4);
             // return -2; // Or return error code if you want to stop here
        }
        if (result <= 0 && result != -1) { // Check if LCM returned 0 unexpectedly
            fprintf(stderr, "Error: Hyperperiod calculation resulted in zero.\n"); return 0;
        }
    }
    printf("System Hyperperiod calculated: %lld\n", result);
    return result;
}

int generate_jobs(long long hyperperiod, const Task tasks_arr[], int task_count, Job jobs_arr[], int* job_count) {
    *job_count = 0; int job_counter = 0;
    printf("Generating job instances up to time %lld...\n", hyperperiod);
    for (int i = 0; i < task_count; i++) {
        int k = 0; long long current_arrival_time;
        while ((current_arrival_time = (long long)tasks_arr[i].arrival_time + (long long)k * tasks_arr[i].period) < hyperperiod) {
            if (*job_count >= MAX_JOBS) { fprintf(stderr, "Error: Exceeded MAX_JOBS (%d) generating.\n", MAX_JOBS); return 0; }
            Job* current_job_ptr = &jobs_arr[*job_count]; // Use pointer for clarity
            current_job_ptr->job_id = job_counter++;
            current_job_ptr->task_id = tasks_arr[i].id;
            current_job_ptr->instance_number = k;
            current_job_ptr->arrival_time = (int)current_arrival_time;
            current_job_ptr->wcet = tasks_arr[i].wcet;
            current_job_ptr->remaining_wcet = tasks_arr[i].wcet;
            current_job_ptr->aet = -1; // Unset
            current_job_ptr->remaining_aet = -1; // Unset
            long long abs_deadline_ll = current_arrival_time + tasks_arr[i].deadline;
            if (abs_deadline_ll > INT_MAX) { fprintf(stderr, "Error: Absolute deadline > INT_MAX for T%d,%d.\n", i, k); return 0; }
            current_job_ptr->absolute_deadline = (int)abs_deadline_ll;
            current_job_ptr->calculated_laxity = INT_MAX; // Initialize
            current_job_ptr->status = NOT_ARRIVED;
            current_job_ptr->first_start_time = -1;
            current_job_ptr->last_start_time = -1;
            current_job_ptr->finish_time = -1;
            current_job_ptr->response_time = -1;
            current_job_ptr->turnaround_time = -1;
            current_job_ptr->waiting_time = -1;
            (*job_count)++; k++;
            if (tasks_arr[i].period <= 0) { fprintf(stderr, "Error: Task %d zero period.\n", i); return 0; }
        }
    }
    printf("Generated %d job instances.\n", *job_count);
    return 1;
}

int read_actual_execution_times(const char* filename, Job jobs_arr[], int job_count) {
    FILE* file = fopen(filename, "r");
    if (!file) { perror("Error opening AET file"); return 0; }
    printf("Reading AETs from %s...\n", filename);
    int jobs_updated = 0; int aet_value; int line_num = 0;
    for (int i = 0; i < job_count; ++i) {
        line_num++;
        if (fscanf(file, "%d", &aet_value) != 1) {
            fprintf(stderr, "Error: Invalid AET format line %d in %s.\n", line_num, filename); fclose(file); return 0;
        }
        if (aet_value <= 0) { fprintf(stderr, "Error: Non-positive AET (%d) job %d line %d.\n", aet_value, i, line_num); fclose(file); return 0; }
        if (aet_value > jobs_arr[i].wcet) {
            fprintf(stderr, "Warning: AET(%d) for J%d line %d > WCET(%d).\n", aet_value, jobs_arr[i].job_id, line_num, jobs_arr[i].wcet);
            // Optionally cap AET at WCET: aet_value = jobs_arr[i].wcet;
        }
        jobs_arr[i].aet = aet_value;
        jobs_arr[i].remaining_aet = aet_value;
        jobs_updated++;
    }
    if (fscanf(file, "%d", &aet_value) != EOF) { fprintf(stderr, "Warning: AET file %s longer than job count (%d).\n", filename, job_count); }
    fclose(file);
    if (jobs_updated != job_count) { fprintf(stderr, "Error: AET count (%d) != job count (%d).\n", jobs_updated, job_count); return 0; }
    printf("Successfully read AET for %d jobs.\n", jobs_updated);
    return 1;
}

void add_job_to_ready_queue(SimulationState* state, Job* job) {
    if (job->status != READY) { return; } // Only add ready jobs
    // Avoid duplicates
    for(int i = 0; i < state->ready_queue_size; ++i) { if (state->ready_queue[i] == job) return; }
    if (state->ready_queue_size < MAX_JOBS) { state->ready_queue[state->ready_queue_size++] = job; }
    else { fprintf(stderr, "CRITICAL Error: Ready queue full...\n"); exit(EXIT_FAILURE); }
}

void remove_job_from_ready_queue(SimulationState* state, Job* job) {
    int i, j;
    for (i = 0; i < state->ready_queue_size; i++) {
        if (state->ready_queue[i] == job) {
            for (j = i; j < state->ready_queue_size - 1; j++) {
                state->ready_queue[j] = state->ready_queue[j + 1];
            }
            state->ready_queue_size--;
            state->ready_queue[state->ready_queue_size] = NULL; // Clear last element
            return; // Found and removed
        }
    }
}

// Helper to calculate laxity for all ready/running jobs
void calculate_all_laxities(SimulationState* state) {
     // Ready queue
    for (int i = 0; i < state->ready_queue_size; i++) {
        Job* job = state->ready_queue[i];
        if (job->status == READY) {
            job->calculated_laxity = job->absolute_deadline - state->current_time - job->remaining_wcet;
        } else {
             job->calculated_laxity = INT_MAX; // Should not happen if queue is clean
        }
    }
    // Running job
    if (state->running_job != NULL && state->running_job->status == RUNNING) {
         state->running_job->calculated_laxity = state->running_job->absolute_deadline - state->current_time - state->running_job->remaining_wcet;
    }
}


// Selects the MLLF task Ta
Job* select_mllf_task_Ta(SimulationState* state) {
    Job* task_Ta = NULL;
    int min_laxity = INT_MAX;
    int min_remaining_wcet_at_min_laxity = INT_MAX;

    if (state->ready_queue_size == 0 && state->running_job == NULL) return NULL; // Nothing to choose from

    // Consider running job as a candidate if it was preempted back to ready
    // Or if it is currently running (for re-evaluation)
    calculate_all_laxities(state); // Ensure laxities are up-to-date

    // Find minimum laxity among ready jobs
    for (int i = 0; i < state->ready_queue_size; i++) {
        Job* current_job = state->ready_queue[i];
        if (current_job->status == READY) { // Only consider ready jobs
            if (current_job->calculated_laxity < min_laxity) {
                min_laxity = current_job->calculated_laxity;
            }
        }
    }
     // Consider running job's laxity as well if it exists
    if (state->running_job != NULL && state->running_job->status == RUNNING) {
         if (state->running_job->calculated_laxity < min_laxity) {
             min_laxity = state->running_job->calculated_laxity;
         }
    }


    // Now find the job(s) with min_laxity and choose the one with min remaining WCET
     // Check running job first (if it has min_laxity)
    if (state->running_job != NULL && state->running_job->status == RUNNING && state->running_job->calculated_laxity == min_laxity) {
        if (state->running_job->remaining_wcet < min_remaining_wcet_at_min_laxity) {
             min_remaining_wcet_at_min_laxity = state->running_job->remaining_wcet;
             task_Ta = state->running_job;
        } else if (state->running_job->remaining_wcet == min_remaining_wcet_at_min_laxity) {
            // Tie break with Job ID if WCETs are also equal
            if (task_Ta == NULL || state->running_job->job_id < task_Ta->job_id) {
                 task_Ta = state->running_job;
            }
        }
    }

    // Check ready queue jobs
    for (int i = 0; i < state->ready_queue_size; i++) {
        Job* current_job = state->ready_queue[i];
        if (current_job->status == READY && current_job->calculated_laxity == min_laxity) {
            if (current_job->remaining_wcet < min_remaining_wcet_at_min_laxity) {
                min_remaining_wcet_at_min_laxity = current_job->remaining_wcet;
                task_Ta = current_job;
            } else if (current_job->remaining_wcet == min_remaining_wcet_at_min_laxity) {
                 // Tie break with Job ID if WCETs are also equal
                 if (task_Ta == NULL || current_job->job_id < task_Ta->job_id) {
                      task_Ta = current_job;
                 }
            }
        }
    }

    return task_Ta;
}


// Finds the deadline of Tmin (earliest deadline job with laxity > Ta's laxity)
int find_earliest_deadline_higher_laxity_job_deadline(SimulationState* state, Job* task_Ta, Job jobs_arr[], int job_count) {
    int earliest_deadline = INT_MAX;
    Job* Tmin_job = NULL;

    if (task_Ta == NULL) return INT_MAX; // Cannot determine Tmin without Ta

    int Ta_laxity = task_Ta->calculated_laxity; // Use pre-calculated laxity

    // Iterate through ALL jobs (ready, running (if not Ta), future arrivals)
    for (int i = 0; i < job_count; i++) {
        Job* current_job = &jobs_arr[i];

        // Skip Ta itself
        if (current_job == task_Ta) continue;

        // Consider only jobs that are active (ready/running) or will arrive
        if (current_job->status == COMPLETED || current_job->status == MISSED) continue;

         // Calculate laxity for comparison job IF it's relevant
         int current_job_laxity = INT_MAX;
         if (current_job->status == READY || current_job->status == RUNNING) {
             current_job_laxity = current_job->calculated_laxity; // Use pre-calculated
         } else if (current_job->status == NOT_ARRIVED) {
             // Laxity doesn't really apply yet, but we need its deadline
             // We only care about jobs with deadlines, assume laxity check passes for future tasks
             // This simplification might need refinement in edge cases, but is often sufficient
             // A stricter check would involve projecting future laxity.
             // Let's only consider jobs whose deadline matters and assume laxity condition holds
             // We are looking for the *earliest deadline constraint*
              if (current_job->absolute_deadline < earliest_deadline) {
                   earliest_deadline = current_job->absolute_deadline;
                   Tmin_job = current_job; // Keep track for debugging/logging if needed
              }
              continue; // Skip laxity check for not arrived tasks in this simplified approach
         }


        // Check MLLF condition: Li(t) > La(t)
        if (current_job_laxity > Ta_laxity) {
            if (current_job->absolute_deadline < earliest_deadline) {
                earliest_deadline = current_job->absolute_deadline;
                Tmin_job = current_job;
            }
        }
    }

    if (Tmin_job == NULL) {
        return NO_TASK_FOUND; // Indicate no Tmin found
    } else {
        return earliest_deadline;
    }
}

// Calculate the execution quantum for Ta
int calculate_mllf_quantum(SimulationState* state, Job* task_Ta, Job jobs_arr[], int job_count) {
    if (task_Ta == NULL || task_Ta->remaining_aet <= 0) {
        return 0; // No quantum if no task or task already finished AET
    }

    int D_min = find_earliest_deadline_higher_laxity_job_deadline(state, task_Ta, jobs_arr, job_count);
    int D_a = task_Ta->absolute_deadline;
    int L_a = task_Ta->calculated_laxity; // Use pre-calculated

    if (D_min == NO_TASK_FOUND || D_a <= D_min) {
        // Case: Ta's deadline is earliest constraint, or no other constraint exists
        // Run until completion (limited by remaining AET)
        return task_Ta->remaining_aet;
    } else {
        // Case: Another task Tmin imposes an earlier constraint
        // Quantum = Dmin(t) - La(t)
        int calculated_quantum = D_min - L_a;

        // Quantum must be at least 1 if calculation <= 0 but work remains
         if (calculated_quantum <= 0 && task_Ta->remaining_aet > 0) {
             calculated_quantum = 1;
         } else if (calculated_quantum < 0) {
             calculated_quantum = 0; // Cannot run backwards
         }


        // Quantum cannot exceed remaining AET
        return (calculated_quantum < task_Ta->remaining_aet) ? calculated_quantum : task_Ta->remaining_aet;
    }
}


void handle_arrivals(SimulationState* state, Job jobs_arr[], int job_count) {
    bool new_arrival = false;
    for (int i = 0; i < job_count; i++) {
        if (jobs_arr[i].status == NOT_ARRIVED && jobs_arr[i].arrival_time == state->current_time) {
            jobs_arr[i].status = READY;
            add_job_to_ready_queue(state, &jobs_arr[i]);
            new_arrival = true; // Flag that an arrival happened
             // Logging handled in run_simulation
        }
    }
    // No return value needed here, rescheduling logic is in the main loop
}

void handle_completion(SimulationState* state) {
     if (state->running_job != NULL && state->running_job->remaining_aet <= 0 && state->running_job->status != COMPLETED && state->running_job->status != MISSED) {
        state->running_job->status = COMPLETED;
        state->running_job->finish_time = state->current_time; // Completed at start of this tick
        (*(state->completed_jobs_ptr))++;
        // Logging handled in run_simulation
        state->running_job = NULL; // CPU is now free
        state->current_job_quantum_remaining = 0; // Reset quantum
    }
}


// Makes MLLF scheduling decision
void make_mllf_scheduling_decision(SimulationState* state, Job* candidate_Ta, char* event_log, size_t log_size, Job jobs_arr[], int job_count) {
    Job* previously_running = state->running_job; // Remember who was running

    if (state->running_job == NULL) { // --- CPU Idle ---
        if (candidate_Ta != NULL) {
            // Start the selected Task Ta
            state->running_job = candidate_Ta;
            state->running_job->status = RUNNING;
            remove_job_from_ready_queue(state, state->running_job); // Remove if it was in ready queue

            // Calculate and set quantum
            state->current_job_quantum_remaining = calculate_mllf_quantum(state, state->running_job, jobs_arr, job_count);

            if (state->running_job->first_start_time == -1) state->running_job->first_start_time = state->current_time;
            state->running_job->last_start_time = state->current_time;

            char start_msg[60];
            snprintf(start_msg, sizeof(start_msg), "Start J%d(L%d,Q%d) ",
                     state->running_job->job_id, state->running_job->calculated_laxity, state->current_job_quantum_remaining);
            strncat(event_log, start_msg, log_size - strlen(event_log) - 1);
        } else {
            // Still Idle
            strncat(event_log, "CPU Idle ", log_size - strlen(event_log) - 1);
            (*(state->idle_time_ptr))++;
            state->current_job_quantum_remaining = 0;
        }
    } else { // --- CPU Busy ---
        if (candidate_Ta == NULL) {
            // This shouldn't happen if running_job is not NULL and not completed, implies error
            // Let running job continue? Or log error? Assume continue for now.
             char cont_msg[60]; snprintf(cont_msg, sizeof(cont_msg), "Continue J%d(L%d,Q%d) ", state->running_job->job_id, state->running_job->calculated_laxity, state->current_job_quantum_remaining);
             strncat(event_log, cont_msg, log_size - strlen(event_log) - 1);

        } else if (candidate_Ta != state->running_job) {
            // Preemption Condition: Rescheduling selected a *different* task Ta
            char preempt_msg[85];
            snprintf(preempt_msg, sizeof(preempt_msg), "Preempt J%d(L%d) for J%d(L%d) ",
                     state->running_job->job_id, state->running_job->calculated_laxity,
                     candidate_Ta->job_id, candidate_Ta->calculated_laxity);
            strncat(event_log, preempt_msg, log_size - strlen(event_log) - 1);

            // Put old job back to ready
            state->running_job->status = READY;
            add_job_to_ready_queue(state, state->running_job);

            // Start the new Ta
            state->running_job = candidate_Ta;
            state->running_job->status = RUNNING;
            remove_job_from_ready_queue(state, state->running_job); // Remove if it was in ready queue

            // Calculate and set quantum for the NEW job
            state->current_job_quantum_remaining = calculate_mllf_quantum(state, state->running_job, jobs_arr, job_count);

            if (state->running_job->first_start_time == -1) state->running_job->first_start_time = state->current_time;
            state->running_job->last_start_time = state->current_time;

             char start_msg[60]; snprintf(start_msg, sizeof(start_msg), "Start J%d(L%d,Q%d) ", state->running_job->job_id, state->running_job->calculated_laxity, state->current_job_quantum_remaining);
             // Check if start message already there due to preemption message structure
             if (strstr(event_log, "Start") == NULL) strncat(event_log, start_msg, log_size - strlen(event_log) - 1);


        } else {
            // Continue Condition: Rescheduling selected the *same* task Ta
            // OR no rescheduling event occurred (quantum not expired, no arrival/completion)

             // We might need to recalculate quantum if an arrival occurred, even if Ta is the same
             // For simplicity, let's assume quantum is recalculated only when Ta changes or expires.
             // A more precise implementation might recalculate quantum on every arrival.

            // Check if quantum needs resetting (e.g., after expiry last tick)
             if (state->current_job_quantum_remaining <= 0 && state->running_job->remaining_aet > 0) {
                 state->current_job_quantum_remaining = calculate_mllf_quantum(state, state->running_job, jobs_arr, job_count);
                  char resetq_msg[60]; snprintf(resetq_msg, sizeof(resetq_msg), "ResetQ J%d(L%d,Q%d) ", state->running_job->job_id, state->running_job->calculated_laxity, state->current_job_quantum_remaining);
                  strncat(event_log, resetq_msg, log_size - strlen(event_log) - 1);
             } else {
                 // Just continue
                  char cont_msg[60]; snprintf(cont_msg, sizeof(cont_msg), "Continue J%d(L%d,Q%d) ", state->running_job->job_id, state->running_job->calculated_laxity, state->current_job_quantum_remaining);
                  // Add "Continue" message only if no other major event occurred for this job this tick
                  if (strstr(event_log, "Completed") == NULL && strstr(event_log, "Preempt") == NULL && strstr(event_log, "Start") == NULL && strstr(event_log, "ResetQ") == NULL) {
                     strncat(event_log, cont_msg, log_size - strlen(event_log) - 1);
                  }
             }
        }
    }

     // Check context switch: only if the running job ID actually changed between *non-idle* states
     int current_running_job_id = (state->running_job == NULL) ? -1 : state->running_job->job_id;
     if (current_running_job_id != state->last_running_job_id && // ID changed
         current_running_job_id != -1 && // New state is not idle
         state->last_running_job_id != -1) // Old state was not idle
         {
          (*(state->context_switches_ptr))++; strncat(event_log, "(CS) ", log_size - strlen(event_log) - 1);
     }
     state->last_running_job_id = current_running_job_id;
}

void execute_running_job(SimulationState* state) {
     if (state->running_job != NULL && state->running_job->status == RUNNING) {
        if (state->running_job->remaining_aet > 0) state->running_job->remaining_aet--;
        if (state->running_job->remaining_wcet > 0) state->running_job->remaining_wcet--;

        // Decrement quantum as well
        if (state->current_job_quantum_remaining > 0) {
             state->current_job_quantum_remaining--;
        }
    }
}

void check_deadline_misses(SimulationState* state, Job jobs_arr[], int job_count, FILE* outfile) {
    int next_time = state->current_time + 1; // Check deadline against the *end* of the current tick

    // Check running job first
    if (state->running_job != NULL && state->running_job->status == RUNNING) {
        // Miss occurs if deadline is *at* or before current time end, and job isn't finished
        if (next_time > state->running_job->absolute_deadline && state->running_job->remaining_aet > 0) {
            fprintf(outfile, "!!! DEADLINE MISS: J%d deadline %d at time %d !!!\n", state->running_job->job_id, state->running_job->absolute_deadline, next_time);
            printf("!!! DEADLINE MISS: J%d deadline %d at time %d !!!\n", state->running_job->job_id, state->running_job->absolute_deadline, next_time);
            state->running_job->status = MISSED;
            (*(state->deadline_misses_ptr))++;
            state->running_job = NULL; // Remove from CPU
            state->current_job_quantum_remaining = 0;
        }
    }

    // Check ready queue
    // Iterate backwards for safe removal
    for (int i = state->ready_queue_size - 1; i >= 0; --i) {
        Job* job_to_check = state->ready_queue[i];
        if (next_time > job_to_check->absolute_deadline) {
            fprintf(outfile, "!!! DEADLINE MISS: J%d deadline %d at time %d !!!\n", job_to_check->job_id, job_to_check->absolute_deadline, next_time);
            printf("!!! DEADLINE MISS: J%d deadline %d at time %d !!!\n", job_to_check->job_id, job_to_check->absolute_deadline, next_time);
            job_to_check->status = MISSED;
            (*(state->deadline_misses_ptr))++;
            remove_job_from_ready_queue(state, job_to_check);
            // No need to adjust index 'i' when iterating backwards
        }
    }
}


// *** Renamed and modified simulation loop ***
void run_mllf_simulation(int hyperperiod, Job jobs_arr[], int job_count, FILE* outfile,
                         int* context_switches, int* deadline_misses, int* completed_jobs, int* idle_time) {

    fprintf(outfile, "\n--- MLLF Simulation Trace (Hyperperiod: %d) ---\n", hyperperiod);
    fprintf(outfile, "Time | Event%-40s | Run Job(L,Q)| Ready Queue (JobId:Laxity)\n", ""); // Adjusted header
    fprintf(outfile, "-----|--------------------------------------------|--------------|--------------------------\n");

    // Initialize simulation state
    SimulationState state;
    state.ready_queue_size = 0;
    state.running_job = NULL;
    state.current_time = 0;
    state.last_running_job_id = -1;
    state.current_job_quantum_remaining = 0; // Init quantum
    state.context_switches_ptr = context_switches;
    state.deadline_misses_ptr = deadline_misses;
    state.completed_jobs_ptr = completed_jobs;
    state.idle_time_ptr = idle_time;
    *context_switches = 0; // Reset counters
    *deadline_misses = 0;
    *completed_jobs = 0;
    *idle_time = 0;


    while (state.current_time < hyperperiod) {
        char event_log[150] = ""; // Event log for the current time tick
        bool requires_reschedule = false; // Flag to force rescheduling

        // Step 1: Handle Arrivals & Check if arrival requires rescheduling
        bool new_arrival_occurred = false;
        for (int i = 0; i < job_count; i++) {
             if (jobs_arr[i].status == NOT_ARRIVED && jobs_arr[i].arrival_time == state.current_time) {
                 jobs_arr[i].status = READY;
                 add_job_to_ready_queue(&state, &jobs_arr[i]);
                 char arrival_msg[40]; snprintf(arrival_msg, sizeof(arrival_msg), "Arrival J%d(T%d) ", jobs_arr[i].job_id, jobs_arr[i].task_id);
                 strncat(event_log, arrival_msg, sizeof(event_log) - strlen(event_log) - 1);
                 requires_reschedule = true; // MLLF reschedules on arrival
                 new_arrival_occurred = true;
             }
        }


        // Step 2: Handle Completion of the previously running job
        bool completion_occurred = false;
         if (state.running_job != NULL && state.running_job->remaining_aet <= 0 && state.running_job->status != COMPLETED && state.running_job->status != MISSED) {
            char complete_msg[40]; snprintf(complete_msg, sizeof(complete_msg), "Complete J%d ", state.running_job->job_id);
            strncat(event_log, complete_msg, sizeof(event_log) - strlen(event_log) - 1);
            handle_completion(&state); // Sets running_job to NULL, increments counter
            completion_occurred = true;
            requires_reschedule = true; // Completion requires rescheduling
         }


        // Step 3: Check for Quantum Expiration
        bool quantum_expired = false;
        if (state.running_job != NULL && state.current_job_quantum_remaining <= 0 && state.running_job->remaining_aet > 0) {
             char quantum_msg[40]; snprintf(quantum_msg, sizeof(quantum_msg), "Quantum Exp J%d ", state.running_job->job_id);
             strncat(event_log, quantum_msg, sizeof(event_log) - strlen(event_log) - 1);
             requires_reschedule = true; // Quantum expiration requires rescheduling
             quantum_expired = true;
             // Do NOT put the job back to ready yet, the scheduler will decide if it continues or gets preempted
        }

        // Step 4: Perform Rescheduling IF NEEDED
        Job* candidate_Ta = NULL;
        if (requires_reschedule || state.running_job == NULL) { // Reschedule if event occurred or CPU idle
             candidate_Ta = select_mllf_task_Ta(&state);
             // Make scheduling decision (handles start/preempt/continue/idle)
             make_mllf_scheduling_decision(&state, candidate_Ta, event_log, sizeof(event_log), jobs_arr, job_count);
        } else {
            // No specific event, running job continues (if any)
            if (state.running_job != NULL) {
                 // Update laxity for logging
                 calculate_all_laxities(&state);
                 char cont_msg[60]; snprintf(cont_msg, sizeof(cont_msg), "Continue J%d(L%d,Q%d) ", state.running_job->job_id, state.running_job->calculated_laxity, state.current_job_quantum_remaining);
                 strncat(event_log, cont_msg, sizeof(event_log) - strlen(event_log) - 1);
            } else {
                 // CPU remains idle
                 strncat(event_log, "CPU Idle ", sizeof(event_log) - strlen(event_log) - 1);
                  (*(state.idle_time_ptr))++; // Increment idle time if no job runs
            }
        }


        // Step 5: Log Current State to File
        fprintf(outfile, "%4d | %-42s | ", state.current_time, event_log);
        if (state.running_job != NULL) { fprintf(outfile, " J%-3d(L%d,Q%d)|", state.running_job->job_id, state.running_job->calculated_laxity, state.current_job_quantum_remaining); }
        else { fprintf(outfile, " %-12s |", "Idle"); }
        fprintf(outfile, " "); int chars_printed = 0;
        // Sort ready queue by laxity for display? Optional. For now, just print.
        for (int i = 0; i < state.ready_queue_size; ++i) {
             chars_printed += fprintf(outfile, "J%d:%d ", state.ready_queue[i]->job_id, state.ready_queue[i]->calculated_laxity);
             if (chars_printed > 18 && i < state.ready_queue_size -1) { fprintf(outfile, "..."); break; }
        }
        fprintf(outfile, "\n");


        // Step 6: Execute Running Job (decrement remaining AET/WCET and quantum)
        execute_running_job(&state);

        // Step 7: Check for Deadline Misses (at the end of the tick)
        check_deadline_misses(&state, jobs_arr, job_count, outfile);

        // Step 8: Advance Time
        state.current_time++;
    } // End simulation loop

    fprintf(outfile, "-----|--------------------------------------------|--------------|--------------------------\n");
}


// --- Analysis Function (Mostly Unchanged, uses calculated values) ---
void analyze_schedule_results(const Job jobs_arr[], int job_count, const Task tasks_arr[], int task_count,
                              int context_switches, int deadline_misses, int completed_jobs, int idle_time,
                              int hyperperiod, FILE* outfile) {

    fprintf(outfile, "\n--- Simulation Analysis ---\n");
    printf("\n--- Simulation Analysis ---\n"); // Mirror summary to console
    fprintf(outfile, "Algorithm: MLLF\n"); printf("Algorithm: MLLF\n"); // Identify algorithm
    fprintf(outfile, "Total time simulated: %d\n", hyperperiod); printf("Total time simulated: %d\n", hyperperiod);
    fprintf(outfile, "Total CPU idle time: %d (%.2f%%)\n", idle_time, hyperperiod > 0 ? (double)idle_time * 100.0 / hyperperiod : 0.0); printf("Total CPU idle time: %d (%.2f%%)\n", idle_time, hyperperiod > 0 ? (double)idle_time * 100.0 / hyperperiod : 0.0);
    fprintf(outfile, "Total jobs generated: %d\n", job_count); printf("Total jobs generated: %d\n", job_count);
    fprintf(outfile, "Total jobs completed: %d\n", completed_jobs); printf("Total jobs completed: %d\n", completed_jobs);
    fprintf(outfile, "Total deadline misses: %d\n", deadline_misses); printf("Total deadline misses: %d\n", deadline_misses);
    fprintf(outfile, "Total context switches: %d\n", context_switches); printf("Total context switches: %d\n", context_switches);
    // fprintf(outfile, "Cache Impact Points (proxy): %d\n", context_switches); printf("Cache Impact Points (proxy): %d\n", context_switches);

    double total_turnaround = 0, total_waiting = 0, total_response = 0;
    int jobs_for_avg = 0;
    int* task_response_times[MAX_TASKS];
    int task_response_counts[MAX_TASKS] = {0};
    int task_response_alloc_size[MAX_TASKS] = {0};
    for (int i = 0; i < task_count; ++i) task_response_times[i] = NULL;

    fprintf(outfile, "\n--- Per-Job Analysis (Completed Jobs) ---\n");
    fprintf(outfile, "JobID | Task(Inst) | Arriv | AET | WCET| Finish | Turnaround | Waiting | Response\n");
    fprintf(outfile, "------|------------|-------|-----|-----|--------|------------|---------|----------\n");
    for (int i = 0; i < job_count; ++i) {
        const Job* job = &jobs_arr[i]; // Use const pointer
        if (job->status == COMPLETED) {
            // Basic sanity check
            if (job->finish_time < job->arrival_time || job->aet < 0) {
                 fprintf(outfile, "Warning: Job J%d timing/AET inconsistent...\n", job->job_id); continue;
            }
             // Response time calculation needs care if first_start_time wasn't recorded correctly
             if (job->first_start_time < job->arrival_time) {
                  // fprintf(outfile, "Warning: Job J%d first_start_time (%d) < arrival_time (%d).\n", job->job_id, job->first_start_time, job->arrival_time);
                  // Set response to 0 or handle as error? Let's assume 0 if start < arrival
                  // response = 0;
             }

            int turnaround = job->finish_time - job->arrival_time;
            int waiting = turnaround - job->aet; // Use actual execution time
            if (waiting < 0) waiting = 0; // Waiting time cannot be negative due to rounding etc.
            int response = (job->first_start_time >= job->arrival_time) ? (job->first_start_time - job->arrival_time) : 0; // Ensure non-negative


            // Update job struct directly (or use separate analysis struct)
            // For simplicity, we update here, though it mixes simulation state with results
            // ((Job*)job)->turnaround_time = turnaround; // Cast away const if needed
            // ((Job*)job)->waiting_time = waiting;
            // ((Job*)job)->response_time = response;


             fprintf(outfile, "J%-4d | T%d(%-2d)    | %5d | %3d | %3d | %6d | %10d | %7d | %8d\n",
                   job->job_id, job->task_id, job->instance_number,
                   job->arrival_time, job->aet, job->wcet, job->finish_time,
                   turnaround, waiting, response);

            total_turnaround += turnaround;
            total_waiting += waiting;
            total_response += response;
            jobs_for_avg++;

            // Store response time for jitter
            int tid = job->task_id;
            if (tid >= 0 && tid < task_count) { // Bounds check
                 if (task_response_counts[tid] >= task_response_alloc_size[tid]) {
                    int new_size = (task_response_alloc_size[tid] == 0) ? 10 : task_response_alloc_size[tid] * 2;
                    if (new_size > MAX_RESPONSE_TIMES_PER_TASK) new_size = MAX_RESPONSE_TIMES_PER_TASK;
                    int* temp = realloc(task_response_times[tid], new_size * sizeof(int));
                    if (!temp && new_size > 0) {
                        fprintf(stderr, "Error: Failed realloc for RT analysis (Task %d, size %d).\n", tid, new_size);
                        // Skip adding this response time if realloc fails
                    } else {
                        task_response_times[tid] = temp;
                        task_response_alloc_size[tid] = new_size;
                         if (task_response_counts[tid] < task_response_alloc_size[tid]) { // Check again after potential realloc
                            task_response_times[tid][task_response_counts[tid]++] = response;
                         } else {
                             // Should not happen if realloc succeeded, but defensive check
                             fprintf(stderr, "Error: RT analysis buffer full post-realloc (Task %d).\n", tid);
                         }
                    }
                 } else {
                     task_response_times[tid][task_response_counts[tid]++] = response;
                 }
            }
        } else if (job->status == MISSED) {
             fprintf(outfile, "J%-4d | T%d(%-2d)    | %5d | %3d | %3d | MISSED D:%-4d| ---        | ---     | ---      \n",
                   job->job_id, job->task_id, job->instance_number, job->arrival_time, job->aet, job->wcet, job->absolute_deadline);
        }
    }

    fprintf(outfile, "\n--- Average Performance Metrics (for Completed Jobs) ---\n");
    printf("\n--- Average Performance Metrics (for Completed Jobs) ---\n");
    if (jobs_for_avg > 0) {
        fprintf(outfile, "Average Turnaround Time: %.2f\n", total_turnaround / jobs_for_avg); printf("Average Turnaround Time: %.2f\n", total_turnaround / jobs_for_avg);
        fprintf(outfile, "Average Waiting Time:    %.2f\n", total_waiting / jobs_for_avg); printf("Average Waiting Time:    %.2f\n", total_waiting / jobs_for_avg);
        fprintf(outfile, "Average Response Time:   %.2f\n", total_response / jobs_for_avg); printf("Average Response Time:   %.2f\n", total_response / jobs_for_avg);
    } else { fprintf(outfile, "No jobs completed successfully.\n"); printf("No jobs completed successfully.\n"); }

    fprintf(outfile, "\n--- Response Time Jitter Analysis (for Completed Jobs) ---\n");
    printf("\n--- Response Time Jitter Analysis (for Completed Jobs) ---\n");
    for (int tid = 0; tid < task_count; ++tid) {
        int count = task_response_counts[tid];
        if (count > 0 && task_response_times[tid] != NULL) {
            int min_rt = INT_MAX, max_rt = INT_MIN, max_rel_jitter = 0;
             int sum_rt = 0; // For average per task
            for (int i = 0; i < count; ++i) {
                int rt = task_response_times[tid][i];
                sum_rt += rt;
                if (rt < min_rt) min_rt = rt; if (rt > max_rt) max_rt = rt;
                if (i > 0) {
                    int diff = abs(rt - task_response_times[tid][i-1]); // Diff between consecutive job instances of same task
                    if (diff > max_rel_jitter) max_rel_jitter = diff;
                }
            }
            int abs_jitter = max_rt - min_rt;
            double avg_rt = (double)sum_rt / count;
            fprintf(outfile, "Task %d: Avg RT=%.2f, Min RT=%d, Max RT=%d, Abs Jitter=%d, Max Rel Jitter=%d (%d samples)\n", tid, avg_rt, min_rt, max_rt, abs_jitter, max_rel_jitter, count);
            printf("Task %d: Avg RT=%.2f, Min RT=%d, Max RT=%d, Abs Jitter=%d, Max Rel Jitter=%d (%d samples)\n", tid, avg_rt, min_rt, max_rt, abs_jitter, max_rel_jitter, count);
        } else { fprintf(outfile, "Task %d: No completed jobs or response times recorded.\n", tid); printf("Task %d: No completed jobs or response times recorded.\n", tid); }
        free(task_response_times[tid]); // Free memory allocated by realloc
    }
     fprintf(outfile, "--------------------------------------------------------\n");
     printf("--------------------------------------------------------\n");
}