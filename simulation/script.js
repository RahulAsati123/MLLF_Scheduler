class MLLFScheduler {
    constructor() {
        this.tasks = [];
        this.jobs = [];
        this.currentTime = 0;
        this.simulationTime = 30;
        this.isRunning = false;
        this.isPaused = false;
        this.readyQueue = [];
        this.runningJob = null;
        this.currentQuantum = 0;
        
        // Statistics
        this.contextSwitches = 0;
        this.deadlineMisses = 0;
        this.completedJobs = 0;
        this.idleTime = 0;
        this.lastRunningJobId = -1;
        
        // Timeline data
        this.timeline = [];
        this.speed = 1000;
        
        this.initializeEventListeners();
    }

    initializeEventListeners() {
        document.getElementById('speedControl').addEventListener('change', (e) => {
            this.speed = parseInt(e.target.value);
        });
    }

    addTask(arrival, period, wcet, deadline, aet) {
        const task = {
            id: this.tasks.length,
            arrival,
            period,
            wcet,
            deadline,
            aet
        };
        this.tasks.push(task);
        this.updateTasksDisplay();
        return task;
    }

    removeTask(taskId) {
        this.tasks = this.tasks.filter(task => task.id !== taskId);
        this.tasks.forEach((task, index) => {
            task.id = index;
        });
        this.updateTasksDisplay();
    }

    generateJobs() {
        this.jobs = [];
        let jobId = 0;

        for (let task of this.tasks) {
            let k = 0;
            let arrivalTime = task.arrival + k * task.period;
            
            while (arrivalTime < this.simulationTime) {
                const job = {
                    jobId: jobId++,
                    taskId: task.id,
                    instanceNumber: k,
                    arrivalTime: arrivalTime,
                    wcet: task.wcet,
                    aet: task.aet,
                    remainingAet: task.aet,
                    remainingWcet: task.wcet,
                    absoluteDeadline: arrivalTime + task.deadline,
                    calculatedLaxity: 0,
                    status: 'NOT_ARRIVED',
                    firstStartTime: -1,
                    lastStartTime: -1,
                    finishTime: -1
                };
                this.jobs.push(job);
                
                k++;
                arrivalTime = task.arrival + k * task.period;
            }
        }
    }

    calculateLaxity(job) {
        if (job.status === 'COMPLETED' || job.status === 'MISSED') {
            return Infinity;
        }
        return job.absoluteDeadline - this.currentTime - job.remainingWcet;
    }

    updateAllLaxities() {
        for (let job of this.readyQueue) {
            job.calculatedLaxity = this.calculateLaxity(job);
        }
        if (this.runningJob) {
            this.runningJob.calculatedLaxity = this.calculateLaxity(this.runningJob);
        }
    }

    selectMLLFTask() {
        if (this.readyQueue.length === 0 && !this.runningJob) {
            return null;
        }

        this.updateAllLaxities();
        
        let candidates = [...this.readyQueue];
        if (this.runningJob && this.runningJob.status === 'RUNNING') {
            candidates.push(this.runningJob);
        }

        if (candidates.length === 0) return null;

        // Find minimum laxity
        let minLaxity = Math.min(...candidates.map(job => job.calculatedLaxity));
        let minLaxityJobs = candidates.filter(job => job.calculatedLaxity === minLaxity);

        // Among jobs with minimum laxity, select the one with minimum remaining WCET
        let minRemainingWcet = Math.min(...minLaxityJobs.map(job => job.remainingWcet));
        let finalCandidates = minLaxityJobs.filter(job => job.remainingWcet === minRemainingWcet);

        // Tie-break with job ID
        return finalCandidates.reduce((min, job) => 
            job.jobId < min.jobId ? job : min
        );
    }

    calculateQuantum(taskTa) {
        if (!taskTa || taskTa.remainingAet <= 0) {
            return 0;
        }

        // Find earliest deadline among jobs with higher laxity
        let earliestDeadline = Infinity;
        let taLaxity = taskTa.calculatedLaxity;

        for (let job of this.jobs) {
            if (job === taskTa || job.status === 'COMPLETED' || job.status === 'MISSED') {
                continue;
            }

            let jobLaxity = this.calculateLaxity(job);
            if (jobLaxity > taLaxity && job.absoluteDeadline < earliestDeadline) {
                earliestDeadline = job.absoluteDeadline;
            }
        }

        if (earliestDeadline === Infinity || taskTa.absoluteDeadline <= earliestDeadline) {
            // Run until completion
            return taskTa.remainingAet;
        } else {
            // Quantum = Dmin - La
            let quantum = earliestDeadline - taLaxity;
            return Math.max(1, Math.min(quantum, taskTa.remainingAet));
        }
    }

    handleArrivals() {
        let arrivals = [];
        for (let job of this.jobs) {
            if (job.status === 'NOT_ARRIVED' && job.arrivalTime === this.currentTime) {
                job.status = 'READY';
                this.readyQueue.push(job);
                arrivals.push(job);
            }
        }
        return arrivals;
    }

    handleCompletion() {
        if (this.runningJob && this.runningJob.remainingAet <= 0 && 
            this.runningJob.status !== 'COMPLETED' && this.runningJob.status !== 'MISSED') {
            
            this.runningJob.status = 'COMPLETED';
            this.runningJob.finishTime = this.currentTime;
            this.completedJobs++;
            
            let completedJob = this.runningJob;
            this.runningJob = null;
            this.currentQuantum = 0;
            
            return completedJob;
        }
        return null;
    }

    makeSchedulingDecision(candidateTa) {
        let events = [];
        let contextSwitch = false;

        if (!this.runningJob) {
            // CPU Idle
            if (candidateTa) {
                this.runningJob = candidateTa;
                this.runningJob.status = 'RUNNING';
                this.readyQueue = this.readyQueue.filter(job => job !== candidateTa);
                this.currentQuantum = this.calculateQuantum(this.runningJob);
                
                if (this.runningJob.firstStartTime === -1) {
                    this.runningJob.firstStartTime = this.currentTime;
                }
                this.runningJob.lastStartTime = this.currentTime;
                
                events.push(`Start J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`);
            } else {
                events.push('CPU Idle');
                this.idleTime++;
            }
        } else {
            // CPU Busy
            if (!candidateTa) {
                events.push(`Continue J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`);
            } else if (candidateTa !== this.runningJob) {
                // Preemption
                events.push(`Preempt J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity}) for J${candidateTa.jobId}(L${candidateTa.calculatedLaxity})`);
                
                this.runningJob.status = 'READY';
                this.readyQueue.push(this.runningJob);
                
                this.runningJob = candidateTa;
                this.runningJob.status = 'RUNNING';
                this.readyQueue = this.readyQueue.filter(job => job !== candidateTa);
                this.currentQuantum = this.calculateQuantum(this.runningJob);
                
                if (this.runningJob.firstStartTime === -1) {
                    this.runningJob.firstStartTime = this.currentTime;
                }
                this.runningJob.lastStartTime = this.currentTime;
                
                contextSwitch = true;
            } else {
                // Continue or reset quantum
                if (this.currentQuantum <= 0 && this.runningJob.remainingAet > 0) {
                    this.currentQuantum = this.calculateQuantum(this.runningJob);
                    events.push(`ResetQ J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`);
                } else {
                    events.push(`Continue J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`);
                }
            }
        }

        // Check for context switch
        let currentRunningJobId = this.runningJob ? this.runningJob.jobId : -1;
        if (currentRunningJobId !== this.lastRunningJobId && 
            currentRunningJobId !== -1 && this.lastRunningJobId !== -1) {
            this.contextSwitches++;
            contextSwitch = true;
        }
        this.lastRunningJobId = currentRunningJobId;

        if (contextSwitch) {
            events.push('(CS)');
        }

        return events;
    }

    executeRunningJob() {
        if (this.runningJob && this.runningJob.status === 'RUNNING') {
            if (this.runningJob.remainingAet > 0) {
                this.runningJob.remainingAet--;
            }
            if (this.runningJob.remainingWcet > 0) {
                this.runningJob.remainingWcet--;
            }
            if (this.currentQuantum > 0) {
                this.currentQuantum--;
            }
        }
    }

    checkDeadlineMisses() {
        let misses = [];
        let nextTime = this.currentTime + 1;

        // Check running job
        if (this.runningJob && this.runningJob.status === 'RUNNING') {
            if (nextTime > this.runningJob.absoluteDeadline && this.runningJob.remainingAet > 0) {
                this.runningJob.status = 'MISSED';
                this.deadlineMisses++;
                misses.push(this.runningJob);
                this.runningJob = null;
                this.currentQuantum = 0;
            }
        }

        // Check ready queue
        for (let i = this.readyQueue.length - 1; i >= 0; i--) {
            let job = this.readyQueue[i];
            if (nextTime > job.absoluteDeadline) {
                job.status = 'MISSED';
                this.deadlineMisses++;
                misses.push(job);
                this.readyQueue.splice(i, 1);
            }
        }

        return misses;
    }

    simulateStep() {
        if (this.currentTime >= this.simulationTime) {
            this.stopSimulation();
            return;
        }

        let events = [];
        let requiresReschedule = false;

        // Handle arrivals
        let arrivals = this.handleArrivals();
        if (arrivals.length > 0) {
            arrivals.forEach(job => {
                events.push(`Arrival J${job.jobId}(T${job.taskId})`);
            });
            requiresReschedule = true;
        }

        // Handle completion
        let completed = this.handleCompletion();
        if (completed) {
            events.push(`Complete J${completed.jobId}`);
            requiresReschedule = true;
        }

        // Check quantum expiration
        if (this.runningJob && this.currentQuantum <= 0 && this.runningJob.remainingAet > 0) {
            events.push(`Quantum Exp J${this.runningJob.jobId}`);
            requiresReschedule = true;
        }

        // Scheduling decision
        let candidateTa = null;
        if (requiresReschedule || !this.runningJob) {
            candidateTa = this.selectMLLFTask();
            let schedulingEvents = this.makeSchedulingDecision(candidateTa);
            events.push(...schedulingEvents);
        } else if (this.runningJob) {
            this.updateAllLaxities();
            events.push(`Continue J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`);
        } else {
            events.push('CPU Idle');
            this.idleTime++;
        }

        // Record timeline
        this.timeline.push({
            time: this.currentTime,
            runningJob: this.runningJob ? { ...this.runningJob } : null,
            readyQueue: this.readyQueue.map(job => ({ ...job })),
            events: [...events]
        });

        // Execute running job
        this.executeRunningJob();

        // Check deadline misses
        let misses = this.checkDeadlineMisses();
        if (misses.length > 0) {
            misses.forEach(job => {
                events.push(`!!! DEADLINE MISS: J${job.jobId} deadline ${job.absoluteDeadline} at time ${this.currentTime + 1} !!!`);
            });
        }

        // Update displays
        this.updateCurrentTimeDisplay();
        this.updateStateDisplay();
        this.updateStatisticsDisplay();
        this.updateTimelineDisplay();
        this.logEvents(events);

        this.currentTime++;
    }

    startSimulation() {
        if (this.tasks.length === 0) {
            alert('Please add at least one task before starting simulation.');
            return;
        }

        this.simulationTime = parseInt(document.getElementById('simTime').value);
        this.generateJobs();
        this.resetSimulation();
        this.isRunning = true;
        
        document.getElementById('startBtn').disabled = true;
        document.getElementById('stepBtn').disabled = false;
        
        this.logMessage('Simulation started', 'event');
        
        this.simulationInterval = setInterval(() => {
            if (!this.isPaused) {
                this.simulateStep();
            }
        }, this.speed);
    }

    stopSimulation() {
        this.isRunning = false;
        if (this.simulationInterval) {
            clearInterval(this.simulationInterval);
        }
        
        document.getElementById('startBtn').disabled = false;
        document.getElementById('stepBtn').disabled = true;
        
        this.logMessage('Simulation completed', 'event');
    }

    resetSimulation() {
        this.currentTime = 0;
        this.readyQueue = [];
        this.runningJob = null;
        this.currentQuantum = 0;
        this.contextSwitches = 0;
        this.deadlineMisses = 0;
        this.completedJobs = 0;
        this.idleTime = 0;
        this.lastRunningJobId = -1;
        this.timeline = [];
        
        if (this.simulationInterval) {
            clearInterval(this.simulationInterval);
        }
        this.isRunning = false;
        this.isPaused = false;
        
        document.getElementById('startBtn').disabled = false;
        document.getElementById('stepBtn').disabled = true;
        
        this.updateCurrentTimeDisplay();
        this.updateStateDisplay();
        this.updateStatisticsDisplay();
        this.updateTimelineDisplay();
        this.clearLog();
        
        this.logMessage('Simulation reset', 'event');
    }

    stepSimulation() {
        if (this.currentTime < this.simulationTime) {
            this.simulateStep();
        } else {
            this.stopSimulation();
        }
    }

    updateTasksDisplay() {
        const tasksList = document.getElementById('tasksList');
        tasksList.innerHTML = '';
        
        this.tasks.forEach(task => {
            const taskDiv = document.createElement('div');
            taskDiv.className = 'task-item';
            taskDiv.innerHTML = `
                <div class="task-header">Task ${task.id}</div>
                <div class="task-details">
                    Arrival: ${task.arrival}, Period: ${task.period}<br>
                    WCET: ${task.wcet}, Deadline: ${task.deadline}, AET: ${task.aet}
                </div>
                <button class="remove-btn" onclick="scheduler.removeTask(${task.id})">×</button>
            `;
            tasksList.appendChild(taskDiv);
        });
    }

    updateCurrentTimeDisplay() {
        document.getElementById('currentTime').textContent = this.currentTime;
    }

    updateStateDisplay() {
        const runningJobElement = document.getElementById('runningJob');
        const readyQueueElement = document.getElementById('readyQueue');
        
        if (this.runningJob) {
            runningJobElement.innerHTML = `J${this.runningJob.jobId}(L${this.runningJob.calculatedLaxity},Q${this.currentQuantum})`;
        } else {
            runningJobElement.textContent = 'None';
        }
        
        if (this.readyQueue.length > 0) {
            readyQueueElement.innerHTML = this.readyQueue
                .map(job => `J${job.jobId}:${job.calculatedLaxity}`)
                .join(', ');
        } else {
            readyQueueElement.textContent = 'Empty';
        }
    }

    updateStatisticsDisplay() {
        document.getElementById('contextSwitches').textContent = this.contextSwitches;
        document.getElementById('deadlineMisses').textContent = this.deadlineMisses;
        document.getElementById('completedJobs').textContent = this.completedJobs;
        document.getElementById('idleTime').textContent = this.idleTime;
    }

    updateTimelineDisplay() {
        const timelineElement = document.getElementById('timeline');
        
        if (this.timeline.length === 0) {
            timelineElement.innerHTML = '<div style="text-align: center; color: #666; padding: 20px;">No simulation data yet</div>';
            return;
        }
        
        // Create timeline visualization
        let html = '';
        
        // CPU timeline
        html += '<div class="timeline-row">';
        html += '<div class="timeline-label">CPU:</div>';
        html += '<div class="timeline-bar">';
        
        for (let i = 0; i < this.timeline.length; i++) {
            const entry = this.timeline[i];
            const width = `${100 / this.timeline.length}%`;
            
            if (entry.runningJob) {
                html += `<div class="timeline-segment running" style="width: ${width}" title="Time ${entry.time}: J${entry.runningJob.jobId}">J${entry.runningJob.jobId}</div>`;
            } else {
                html += `<div class="timeline-segment idle" style="width: ${width}" title="Time ${entry.time}: Idle">-</div>`;
            }
        }
        
        html += '</div></div>';
        
        timelineElement.innerHTML = html;
    }

    logEvents(events) {
        events.forEach(event => {
            if (event.includes('DEADLINE MISS')) {
                this.logMessage(`Time ${this.currentTime}: ${event}`, 'error');
            } else {
                this.logMessage(`Time ${this.currentTime}: ${event}`, 'event');
            }
        });
    }

    logMessage(message, type = 'info') {
        const logElement = document.getElementById('simulationLog');
        const logEntry = document.createElement('div');
        logEntry.className = `log-entry ${type}`;
        logEntry.textContent = message;
        logElement.appendChild(logEntry);
        logElement.scrollTop = logElement.scrollHeight;
    }

    clearLog() {
        document.getElementById('simulationLog').innerHTML = '';
    }
}

// Global scheduler instance
const scheduler = new MLLFScheduler();

// Event handlers
function addTask() {
    const arrival = parseInt(document.getElementById('arrival').value);
    const period = parseInt(document.getElementById('period').value);
    const wcet = parseInt(document.getElementById('wcet').value);
    const deadline = parseInt(document.getElementById('deadline').value);
    const aet = parseInt(document.getElementById('aet').value);
    
    if (wcet > deadline) {
        alert('Warning: WCET is greater than deadline. This task may not be schedulable.');
    }
    
    if (aet > wcet) {
        alert('Warning: AET is greater than WCET. AET will be capped at WCET.');
        aet = wcet;
    }
    
    scheduler.addTask(arrival, period, wcet, deadline, aet);
    
    // Clear inputs
    document.getElementById('arrival').value = '0';
    document.getElementById('period').value = '10';
    document.getElementById('wcet').value = '3';
    document.getElementById('deadline').value = '10';
    document.getElementById('aet').value = '2';
}

function startSimulation() {
    scheduler.startSimulation();
}

function resetSimulation() {
    scheduler.resetSimulation();
}

function stepSimulation() {
    scheduler.stepSimulation();
}

// Initialize with sample tasks
document.addEventListener('DOMContentLoaded', function() {
    scheduler.addTask(0, 10, 3, 10, 2);
    scheduler.addTask(0, 15, 4, 15, 3);
    scheduler.logMessage('Sample tasks added. Configure tasks and start simulation.', 'info');
});
