#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#include "session.hxx"
#include "subscriber.hxx"

class Receiver {
   public:
    Receiver(zenoh::Session& session, const std::string& key_expr, size_t ring_length = 0,
             std::function<void()> on_drop = nullptr)
        : _ring_length(ring_length),
          _on_drop(std::move(on_drop)),
          _subscriber(session.declare_subscriber(
              key_expr,
              [this](zenoh::Sample sample) {
                  std::unique_lock lock(_mutex);
                  if (_ring_length > 0 && _buffer.size() >= _ring_length) _buffer.pop_front();
                  _buffer.push_back(std::move(sample));
                  _cond.notify_one();
              },
              zenoh::closures::none)) {
    }

    std::optional<zenoh::Sample> receive(unsigned int timeout_ms) {
        std::unique_lock lock(_mutex);
        if (_buffer.empty()) {
            if (!_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !_buffer.empty(); }))
                return std::nullopt;  // Timeout
        }

        zenoh::Sample sample = std::move(_buffer.front());  // move from front
        _buffer.pop_front();
        return sample;  // returned by move
    }

    ~Receiver() {
        if (_on_drop) _on_drop();  // let ~Subscriber handle undeclare
    }

   private:
    size_t _ring_length;
    std::function<void()> _on_drop;
    std::deque<zenoh::Sample> _buffer;
    std::mutex _mutex;
    std::condition_variable _cond;
    zenoh::Subscriber<void> _subscriber;
};
