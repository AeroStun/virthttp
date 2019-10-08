#pragma once

#ifdef VIRTHTTP_ENABLE_HTTP2

#include <iostream>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <nghttp2/nghttp2.h>
#include "wrapper/general_store.hpp"
#include "utils.hpp"

namespace http2 {

class Session;
class Stream;

class Session {
    nghttp2_session* p = nullptr;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::ip::tcp::socket::executor_type> strand_;
    boost::beast::flat_buffer buffer_;
    std::reference_wrapper<GeneralStore> m_gstore;

  public:
    Session(boost::beast::net::ip::tcp::socket socket, GeneralStore& gstore) noexcept;

    void run() { do_read(); }
    void do_read();
};

Session::Session(boost::beast::net::ip::tcp::socket socket, GeneralStore& gstore) noexcept
    : socket_(std::move(socket)), strand_(socket_.get_executor()), m_gstore(gstore) {
    nghttp2_session_callbacks* cbs = nullptr;
    auto cbs_del = ScopeGuard{nghttp2_session_callbacks_del, cbs};
    {
        const auto res = nghttp2_session_callbacks_new(&cbs);
        switch (res) {
        case 0: // Success
            break;
        case NGHTTP2_ERR_NOMEM:
            std::cerr << "Could not create http2::session callbacks: out of memory\n" << std::flush;
        default:
            return;
        }
    }
    nghttp2_session_callbacks_set_send_callback(
        cbs, +[](nghttp2_session* session, const uint8_t* data, size_t length, int flags, void* user_data) -> ssize_t {
            auto& beast_sess = *reinterpret_cast<Session*>(user_data);
        });
    const auto res = nghttp2_session_server_new(&p, cbs, this);
}

} // namespace http2

#endif