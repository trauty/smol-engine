#include "jolt_job_system_int.h"
#include "Jolt/Core/Core.h"
#include "Jolt/Core/JobSystem.h"

#include "smol/jobs.h"

namespace smol
{
    int jolt_job_system_integration_t::GetMaxConcurrency() const { return (int)smol::jobs::get_worker_count(); }

    JPH::JobHandle jolt_job_system_integration_t::CreateJob(const char* in_name, JPH::ColorArg in_color,
                                                            const JPH::JobSystem::JobFunction& in_job_func,
                                                            JPH::uint32 in_num_deps)
    {
        JPH::JobSystem::Job* job = new JPH::JobSystem::Job(in_name, in_color, this, in_job_func, in_num_deps);
        JPH::JobHandle handle(job);
        return handle;
    }

    void jolt_job_system_integration_t::QueueJob(JPH::JobSystem::Job* in_job)
    {
        in_job->AddRef();
        smol::jobs::kick(
            [in_job]() {
                in_job->Execute();
                in_job->Release();
            },
            nullptr, smol::jobs::priority_e::HIGH);
    }

    void jolt_job_system_integration_t::QueueJobs(JPH::JobSystem::Job** in_jobs, JPH::uint32 in_num_jobs)
    {
        for (JPH::uint32 i = 0; i < in_num_jobs; i++) { QueueJob(in_jobs[i]); }
    }

    void jolt_job_system_integration_t::FreeJob(JPH::JobSystem::Job* in_job) { delete in_job; }
} // namespace smol