#include "scheduler.h"

/*
 * Non-preemptive scheduling (FCFS, PRIORITY) runs on the shared
 * event-driven engine with preemption switched off, so that resource
 * blocking and wake-up are handled in one place (engine.c).
 */
int run_nonpreemptive(Patient patients[], int n, BetterFn better)
{
    return run_event_engine(patients, n, better, 0);
}
