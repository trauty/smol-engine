#pragma once

#include "smol/ecs_types.h"
#include "smol/hash.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace smol::ecs
{
    constexpr entity_t MAX_ENTITIES = 200000;

    template<typename T>
    consteval size_t get_component_id()
    {
        static_assert(is_component_con<T>,
                      "One of the components has no id. Register it with SMOL_COMPONENT(Name) inside the component.");
        return hash::hashString64(T::get_type_name());
    }

    struct pool_t
    {
        virtual ~pool_t() = default;
        virtual void remove(entity_t entity) = 0;
        virtual bool has(entity_t entity) const = 0;
        virtual void clear() = 0;
        virtual void reorder(const std::vector<entity_t>& ordered_entities) = 0;
    };

    class registry_t;

    template<typename T>
    class sparse_set_t : public pool_t
    {
      public:
        using destroy_callback_t = std::function<void(registry_t&, entity_t, T&)>;
        destroy_callback_t on_destroy_cb = nullptr;

        registry_t* registry_ref = nullptr;

        static constexpr size_t PAGE_SIZE = 4096;
        static constexpr size_t PAGE_SHIFT = 12;
        static constexpr size_t OFFSET_MASK = PAGE_SIZE - 1;

        sparse_set_t() = default;

        size_t get_index(entity_t entity) const
        {
            const size_t* sparse_ptr = try_get_sparse_ptr(entity);
            return sparse_ptr ? *sparse_ptr : NULL_ENTITY;
        }

        bool has(entity_t entity) const override
        {
            const size_t* sparse_ptr = try_get_sparse_ptr(entity);
            return sparse_ptr && *sparse_ptr != NULL_ENTITY;
        }

        template<typename... Args>
        T& emplace(entity_t entity, Args&&... args)
        {
            if (has(entity)) { return dense_data[get_index(entity)]; }

            size_t index = dense_data.size();

            *get_sparse_ptr(entity) = index;

            dense_data.emplace_back(std::forward<Args>(args)...);
            dense_entity.push_back(entity);

            return dense_data.back();
        }

        void remove(entity_t entity) override
        {
            if (!has(entity)) { return; }

            if (on_destroy_cb && registry_ref)
            {
                T& comp = dense_data[get_index(entity)];
                on_destroy_cb(*registry_ref, entity, comp);
            }

            size_t idx_removed = *get_sparse_ptr(entity);
            size_t idx_last = dense_data.size() - 1;
            entity_t entity_last = dense_entity[idx_last];

            dense_data[idx_removed] = std::move(dense_data[idx_last]);
            dense_entity[idx_removed] = entity_last;

            *get_sparse_ptr(entity_last) = idx_removed;
            *get_sparse_ptr(entity) = NULL_ENTITY;

            dense_data.pop_back();
            dense_entity.pop_back();
        }

        T& get(entity_t entity)
        {
            assert(has(entity));
            return dense_data[get_index(entity)];
        }

        T* data() { return dense_data.data(); }
        const entity_t* entities() const { return dense_entity.data(); }
        size_t size() { return dense_data.size(); }

        void clear() override
        {
            if (on_destroy_cb && registry_ref)
            {
                for (size_t i = 0; i < dense_data.size(); i++)
                {
                    on_destroy_cb(*registry_ref, dense_entity[i], dense_data[i]);
                }
            }

            dense_data.clear();
            dense_entity.clear();

            for (std::unique_ptr<size_t[]>& page : sparse_pages)
            {
                if (page) { std::fill_n(page.get(), PAGE_SHIFT, NULL_ENTITY); }
            }
        }

        void reorder(const std::vector<entity_t>& ordered_entities) override
        {
            if (ordered_entities.size() != dense_entity.size()) { return; }

            std::vector<T> new_data;
            new_data.reserve(dense_data.size());
            std::vector<entity_t> new_entities;
            new_entities.reserve(dense_entity.size());

            for (entity_t entity : ordered_entities)
            {
                if (has(entity))
                {
                    size_t old_index = get_index(entity);
                    new_data.push_back(std::move(dense_data[old_index]));
                    new_entities.push_back(entity);
                }
            }

            dense_data = std::move(new_data);
            dense_entity = std::move(new_entities);

            for (size_t i = 0; i < dense_entity.size(); i++) { *get_sparse_ptr(dense_entity[i]) = i; }
        }

        const std::vector<entity_t>& get_entities() const { return dense_entity; }

      private:
        std::vector<T> dense_data;
        std::vector<entity_t> dense_entity;
        std::vector<std::unique_ptr<size_t[]>> sparse_pages;

        size_t* get_sparse_ptr(entity_t entity)
        {
            size_t page = entity >> PAGE_SHIFT;
            size_t offset = entity & OFFSET_MASK;
            if (page >= sparse_pages.size()) { sparse_pages.resize(page + 1); }
            if (!sparse_pages[page])
            {
                sparse_pages[page] = std::make_unique<size_t[]>(PAGE_SIZE);
                std::fill_n(sparse_pages[page].get(), PAGE_SIZE, NULL_ENTITY);
            }

            return &sparse_pages[page][offset];
        }

        const size_t* try_get_sparse_ptr(entity_t entity) const
        {
            size_t page = entity >> PAGE_SHIFT;
            if (page >= sparse_pages.size() || !sparse_pages[page]) { return nullptr; }
            return &sparse_pages[page][entity & OFFSET_MASK];
        }
    };

    class registry_t
    {
      public:
        entity_t create();
        void destroy(entity_t entity);

        template<typename T>
        sparse_set_t<T>& get_storage()
        {
            return get_pool<T>();
        }

        template<typename T, typename... Args>
        T& emplace(entity_t entity, Args&&... args)
        {
            return get_pool<T>().emplace(entity, std::forward<Args>(args)...);
        }

        template<typename T>
        void remove_component(entity_t entity)
        {
            get_pool<T>().remove(entity);
        }

        template<typename T>
        void on_destroy(std::function<void(registry_t&, entity_t, T&)> cb)
        {
            sparse_set_t<T>& pool = get_storage<T>();
            pool.on_destroy_cb = cb;
            pool.registry_ref = this;
        }

        template<typename T>
        T& get(entity_t entity)
        {
            return get_pool<T>().get(entity);
        }

        template<typename T>
        bool has(entity_t entity) const
        {
            auto* pool = try_get_pool<T>();
            return pool && pool->has(entity);
        }

        template<typename T>
        void set_context(T* instance)
        {
            std::scoped_lock lock(ctx_mutex);
            contexts[get_component_id<T>()] = instance;
        }

        template<typename T>
        T* ctx() const
        {
            size_t id = get_component_id<T>();

            auto it = contexts.find(id);
            if (it != contexts.end()) { return static_cast<T*>(it->second); }

            return nullptr;
        }

        template<typename First, typename... Rest>
        class view_t
        {
          public:
            struct iterator_t
            {
                typename std::vector<entity_t>::const_iterator cur_entity_it;
                typename std::vector<entity_t>::const_iterator end_entity_it;

                sparse_set_t<First>* lead_pool;
                std::tuple<sparse_set_t<Rest>*...> rest_pools;

                iterator_t(typename std::vector<entity_t>::const_iterator start,
                           typename std::vector<entity_t>::const_iterator end, sparse_set_t<First>* lead,
                           std::tuple<sparse_set_t<Rest>*...> rest)
                    : cur_entity_it(start), end_entity_it(end), lead_pool(lead), rest_pools(rest)
                {
                    if (cur_entity_it != end_entity_it && !is_valid()) { ++(*this); }
                }

                bool is_valid() const
                {
                    entity_t entity = *cur_entity_it;

                    // has(entity) on all rest pools?
                    return std::apply([entity](auto... pools) { return ((pools->has(entity)) && ...); }, rest_pools);
                }

                iterator_t& operator++()
                {
                    do { cur_entity_it++; }
                    while (cur_entity_it != end_entity_it && !is_valid());

                    return *this;
                }

                bool operator!=(const iterator_t& other) const { return cur_entity_it != other.cur_entity_it; }

                bool operator==(const iterator_t& other) const { return cur_entity_it == other.cur_entity_it; }

                std::tuple<entity_t, First&, Rest&...> operator*()
                {
                    entity_t entity = *cur_entity_it;
                    return std::tuple<entity_t, First&, Rest&...>(
                        entity, lead_pool->get(entity), std::get<sparse_set_t<Rest>*>(rest_pools)->get(entity)...);
                }
            };

            view_t(registry_t& reg)
            {
                lead_pool = &reg.get_pool<First>();
                rest_pools = std::make_tuple(&reg.get_pool<Rest>()...);
            }

            iterator_t begin()
            {
                return iterator_t(lead_pool->get_entities().begin(), lead_pool->get_entities().end(), lead_pool,
                                  rest_pools);
            }

            iterator_t end()
            {
                return iterator_t(lead_pool->get_entities().end(), lead_pool->get_entities().end(), lead_pool,
                                  rest_pools);
            }

          private:
            sparse_set_t<First>* lead_pool;
            std::tuple<sparse_set_t<Rest>*...> rest_pools;
        };

        template<typename First, typename... Rest>
        view_t<First, Rest...> view()
        {
            return view_t<First, Rest...>(*this);
        }

      private:
        std::atomic<entity_t> entity_counter{0};
        std::mutex recycle_mutex;
        std::vector<entity_t> free_entities;

        std::unordered_map<size_t, std::unique_ptr<pool_t>> pools;
        std::unordered_map<size_t, void*> contexts;
        mutable std::mutex ctx_mutex;

        template<typename T>
        sparse_set_t<T>& get_pool()
        {
            constexpr size_t id = get_component_id<T>();
            auto it = pools.find(id);
            if (it != pools.end()) { return *static_cast<sparse_set_t<T>*>(it->second.get()); }

            std::unique_ptr<sparse_set_t<T>> ptr = std::make_unique<sparse_set_t<T>>();
            ptr->registry_ref = this;
            sparse_set_t<T>* raw_ptr = ptr.get();
            pools[id] = std::move(ptr);
            return *raw_ptr;
        }

        template<typename T>
        sparse_set_t<T>* try_get_pool() const
        {
            auto it = pools.find(get_component_id<T>());
            if (it != pools.end()) return static_cast<sparse_set_t<T>*>(it->second.get());
            return nullptr;
        }
    };
} // namespace smol::ecs