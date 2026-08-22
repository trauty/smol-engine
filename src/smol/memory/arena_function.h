#pragma once

#include "smol/log.h"
#include "smol/memory/linear_allocator.h"

#include <type_traits>
#include <utility>

namespace smol
{
    template <typename Signature>
    struct arena_function;

    template <typename Ret, typename... Args>
    struct arena_function<Ret(Args...)>
    {
        void* capture_data = nullptr;
        Ret (*invoke_func)(void* data, Args... args) = nullptr;

        arena_function() = default;

        template <typename Lambda>
        arena_function(Lambda&& lambda)
        {
            using decayed_lambda_t = std::decay_t<Lambda>;

            // The arena is reset by moving an offset back to zero, so nothing placed in it is
            // ever destroyed. A capture that owns memory (std::string, std::function, a vector)
            // would leak its buffer on every frame the callback is rebuilt.
            static_assert(std::is_trivially_destructible_v<decayed_lambda_t>,
                          "arena_function captures must be trivially destructible -- the arena never "
                          "runs destructors, so an owning capture leaks every frame");

            linear_allocator_t* arena = get_active_arena();
            if (!arena)
            {
                SMOL_LOG_FATAL("MEMORY", "Tried to create an arena_function without an active arena");
                abort();
            }

            void* memory = arena->allocate(sizeof(decayed_lambda_t), alignof(decayed_lambda_t));
            decayed_lambda_t* placed_lambda = new (memory) decayed_lambda_t(std::forward<Lambda>(lambda));

            capture_data = placed_lambda;
            invoke_func = [](void* raw_data, Args... args) -> Ret
            {
                decayed_lambda_t* typed_lambda = static_cast<decayed_lambda_t*>(raw_data);
                return (*typed_lambda)(std::forward<Args>(args)...);
            };
        }

        Ret operator()(Args... args) const { return invoke_func(capture_data, std::forward<Args>(args)...); }

        explicit operator bool() const { return invoke_func != nullptr; }
    };

    template <typename Signature>
    struct arena_function_traits;

    template <typename Ret, typename... Args>
    struct arena_function_traits<Ret(Args...)>
    {
        template <typename Allocator, typename Lambda>
        static arena_function<Ret(Args...)> bind(Allocator& allocator, Lambda&& lambda)
        {
            using decayed_lambda_t = std::decay_t<Lambda>;

            void* memory = allocator.allocate(sizeof(decayed_lambda_t), alignof(decayed_lambda_t));

            decayed_lambda_t* placed_lambda = new (memory) decayed_lambda_t(std::forward<Lambda>(lambda));

            arena_function<Ret(Args...)> func;
            func.capture_data = placed_lambda;

            func.invoke_func = [](void* raw_data, Args... args) -> Ret
            {
                decayed_lambda_t* typed_lambda = static_cast<decayed_lambda_t*>(raw_data);
                return (*typed_lambda)(std::forward<Args>(args)...);
            };

            return func;
        }
    };

    template <typename Signature, typename Allocator, typename Lambda>
    arena_function<Signature> make_arena_function(Allocator& allocator, Lambda&& lambda)
    { return arena_function_traits<Signature>::bind(allocator, std::forward<Lambda>(lambda)); }
} // namespace smol