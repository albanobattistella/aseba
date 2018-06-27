#pragma once
#include <memory>
#include <mutex>
#include "aseba/common/msg/msg.h"
#include <boost/asio/io_context.hpp>
#include <atomic>


namespace mobsya {
class aseba_endpoint;

class aseba_node : public std::enable_shared_from_this<aseba_node> {
public:
    // aseba_node::status is exposed through zero conf & protocol : needs to be stable
    enum class status { connected = 1, ready = 2, busy = 3, disconnected = 4 };
    using node_id_t = uint16_t;

    aseba_node(boost::asio::io_context& ctx, node_id_t id, std::weak_ptr<mobsya::aseba_endpoint> endpoint);

    ~aseba_node();

    static std::shared_ptr<aseba_node> create(boost::asio::io_context& ctx, node_id_t id,
                                              std::weak_ptr<mobsya::aseba_endpoint> endpoint);

    static const std::string& status_to_string(aseba_node::status);
    status get_status() const {
        return m_status.load();
    }

    node_id_t native_id() const {
        return m_id;
    }

    Aseba::TargetDescription vm_description() const {
        std::unique_lock<std::mutex> _(m_node_mutex);
        return m_description;
    }

    void write_messages(std::vector<std::shared_ptr<Aseba::Message>>&& message);
    void write_message(std::shared_ptr<Aseba::Message> message);

    bool send_aseba_program(const std::string& program);
    void run_aseba_program();

    bool lock(void* app);
    bool unlock(void* app);

private:
    friend class aseba_endpoint;
    void set_status(status);

    // Must be called before destructor !
    void disconnect();
    void on_message(const Aseba::Message& msg);
    void on_description(Aseba::TargetDescription description);

    node_id_t m_id;
    std::atomic<status> m_status;
    std::atomic<void*> m_connected_app;
    std::weak_ptr<mobsya::aseba_endpoint> m_endpoint;
    mutable std::mutex m_node_mutex;
    Aseba::TargetDescription m_description;
    boost::asio::io_context& m_io_ctx;
};


}  // namespace mobsya
