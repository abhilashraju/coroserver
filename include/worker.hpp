#pragma once
#include "make_awaitable.hpp"

#include <deque>
#include <functional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>
namespace NSNAME
{
struct WorkerPool
{
    std::deque<std::function<void()>> task_queue;
    std::mutex mutex;
    std::condition_variable condition;
    std::stop_source stop_source;
    std::vector<std::jthread> threads;
    std::stop_token stopToken() const
    {
        return stop_source.get_token();
    }
    WorkerPool(unsigned num_threads = std::thread::hardware_concurrency()) :
        threads(num_threads)
    {
        run();
    }
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;
    void addTask(std::function<void()>&& task)
    {
        std::unique_lock<std::mutex> lock(mutex);
        task_queue.emplace_back(std::move(task));
        condition.notify_all();
    }
    void run()
    {
        for (auto& thread : threads)
        {
            thread = std::jthread([this]() {
                auto token = stopToken();
                while (!token.stop_requested())
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        condition.wait(lock, [this, token] {
                            return !task_queue.empty() ||
                                   token.stop_requested();
                        });
                        if (!token.stop_requested())
                        {
                            task = std::move(task_queue.front());
                            task_queue.pop_front();
                        }
                    }
                    if (!task)
                        break;
                    task();
                }
            });
        }
    }

    ~WorkerPool()
    {
        stop_source.request_stop();
        condition.notify_all();
    }
};
inline WorkerPool& getWorkerPool()
{
    static WorkerPool pool(1);
    return pool;
}
template <typename RetType>
inline AwaitableResult<boost::system::error_code, RetType> asyncCall(
    net::io_context& ctx, std::function<RetType()>&& task)
{
    auto& pool = getWorkerPool();
    auto h = make_awaitable_handler<RetType>([&](auto promise) {
        auto promise_ptr =
            std::make_shared<decltype(promise)>(std::move(promise));
        pool.addTask([&ctx, promise_ptr, task = std::move(task)]() mutable {
            try
            {
                RetType ret = task();
                LOG_DEBUG(
                    "Task completed successfully Posting result to process in io context");
                ctx.post([promise_ptr, ret = std::move(ret)]() mutable {
                    LOG_DEBUG("Setting promise values in io context");
                    (*promise_ptr)
                        .setValues(boost::system::error_code{}, std::move(ret));
                });
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Exception in task: {}", e.what());
                LOG_DEBUG("Posting error to process in io context");
                ctx.post([promise_ptr]() mutable {
                    (*promise_ptr)
                        .setValues(
                            boost::system::error_code{
                                boost::system::errc::make_error_code(
                                    boost::system::errc::operation_canceled)},
                            RetType{});
                });
            }
        });
    });
    co_return co_await h();
}

inline AwaitableResult<boost::system::error_code> asyncCall(
    net::io_context& ctx, std::function<void()>&& task)
{
    auto& pool = getWorkerPool();
    auto h = make_awaitable_handler<
        boost::system::error_code>([&](auto promise) {
        auto promise_ptr =
            std::make_shared<decltype(promise)>(std::move(promise));
        pool.addTask([&ctx, promise_ptr, task = std::move(task)]() mutable {
            try
            {
                task();
                LOG_DEBUG(
                    "Task completed successfully Posting result to process in io context");
                ctx.post([promise_ptr]() mutable {
                    LOG_DEBUG("Setting promise values in io context");
                    (*promise_ptr).setValues(boost::system::error_code{});
                });
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Exception in task: {}", e.what());
                LOG_DEBUG("Posting error to process in io context");
                ctx.post([promise_ptr]() mutable {
                    (*promise_ptr)
                        .setValues(boost::system::error_code{
                            boost::system::errc::make_error_code(
                                boost::system::errc::operation_canceled)});
                });
            }
        });
    });
    co_return co_await h();
}
}