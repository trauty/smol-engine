#pragma once

// clang-format off
#include <Jolt/Jolt.h>
#include "Jolt/Core/Core.h"
#include "Jolt/Core/JobSystem.h"
#include <Jolt/Core/JobSystemWithBarrier.h>
// clang-format on

namespace smol
{
    class jolt_job_system_integration_t : public JPH::JobSystemWithBarrier
    {
      public:
        jolt_job_system_integration_t() : JPH::JobSystemWithBarrier(1024) {}

        virtual int GetMaxConcurrency() const override;
        virtual JPH::JobHandle CreateJob(const char* in_name, JPH::ColorArg in_color,
                                         const JPH::JobSystem::JobFunction& in_job_func,
                                         JPH::uint32 in_num_deps = 0) override;
        virtual void QueueJob(JPH::JobSystem::Job* in_job) override;
        virtual void QueueJobs(JPH::JobSystem::Job** in_jobs, JPH::uint32 in_num_jobs) override;
        virtual void FreeJob(JPH::JobSystem::Job* in_job) override;
    };
} // namespace smol