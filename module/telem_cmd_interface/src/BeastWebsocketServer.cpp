/**
 * @file BeastWebsocketServer.cpp
 * @author  Brian Tomko <brian.j.tomko@nasa.gov>
 *
 * @copyright Copyright © 2022 United States Government as represented by
 * the National Aeronautics and Space Administration.
 * No copyright is claimed in the United States under Title 17, U.S.Code.
 * All Other Rights Reserved.
 *
 * @section LICENSE
 * Released under the NASA Open Source Agreement (NOSA)
 * See LICENSE.md in the source root directory for more information.
 * This source file is based on the example "Advanced server, flex (plain + SSL)" from
 * https://raw.githubusercontent.com/boostorg/beast/b07edea9d70ed5a613879ab94896ed9b7255f5a8/example/advanced/server-flex/advanced_server_flex.cpp
 * Which holds the following copyright and license:
 * Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "BeastWebsocketServer.h"
#include "Logger.h"

static const hdtn::Logger::SubProcess subprocess = hdtn::Logger::SubProcess::gui;

#define BEAST_WEBSOCKET_SERVER_NUM_THREADS 1

#ifndef BEAST_WEBSOCKET_SERVER_NUM_THREADS
#error "BEAST_WEBSOCKET_SERVER_NUM_THREADS must be defined"
#elif (BEAST_WEBSOCKET_SERVER_NUM_THREADS < 1)
#error "BEAST_WEBSOCKET_SERVER_NUM_THREADS must be at least 1"
#elif (BEAST_WEBSOCKET_SERVER_NUM_THREADS == 1)
#define BEAST_WEBSOCKET_SERVER_SINGLE_THREADED 1
#endif // ! BEAST_WEBSOCKET_SERVER_NUM_THREADS



//https://raw.githubusercontent.com/boostorg/beast/b07edea9d70ed5a613879ab94896ed9b7255f5a8/example/advanced/server-flex/advanced_server_flex.cpp


#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
#include <boost/beast/ssl.hpp>
#endif
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <boost/thread.hpp>
#include "ThreadNamer.h"
#include <vector>
#include <array>
#include <atomic>


#if (__cplusplus >= 201703L)
#include <shared_mutex>
typedef std::shared_mutex shared_mutex_t;
typedef std::shared_lock<std::shared_mutex> shared_lock_t;
typedef std::unique_lock<std::shared_mutex> exclusive_lock_t;
#else
typedef boost::shared_mutex shared_mutex_t;
typedef boost::shared_lock<boost::shared_mutex> shared_lock_t;
typedef boost::unique_lock<boost::shared_mutex> exclusive_lock_t;
#endif

struct ServerState : private boost::noncopyable {
    ServerState() = delete;
    ServerState(const std::string& docRoot,
        const OnNewBeastWebsocketConnectionCallback_t& connectionCallback,
        const OnNewBeastWebsocketDataReceivedCallback_t& dataCallback) :
        m_docRoot(docRoot),
        m_onNewWebsocketConnectionCallback(connectionCallback),
        m_onNewWebsocketDataReceivedCallback(dataCallback),
        m_nextWebsocketConnectionIdAtomic(0) {}

    const std::string m_docRoot;
    const OnNewBeastWebsocketConnectionCallback_t m_onNewWebsocketConnectionCallback;
    const OnNewBeastWebsocketDataReceivedCallback_t m_onNewWebsocketDataReceivedCallback;
    boost::mutex m_activeConnectionsMutex;
    shared_mutex_t m_unclosedConnectionsSharedMutex;
    std::atomic_uint32_t m_nextWebsocketConnectionIdAtomic;
    typedef std::map<uint32_t, std::shared_ptr<WebsocketSessionBase> > active_connections_map_t;
    active_connections_map_t m_activeConnections; //allow multiple connections
};
typedef std::shared_ptr<ServerState> ServerState_ptr;

static boost::beast::string_view GetExtension(const boost::beast::string_view& path) {
    const std::size_t pos = path.rfind(".");
    if (pos == boost::beast::string_view::npos) {
        return boost::beast::string_view{};
    }
    return path.substr(pos);
}

// Return a reasonable mime type based on the extension of a file.
static boost::beast::string_view MimeType(const boost::beast::string_view& path) {
    const boost::beast::string_view ext = GetExtension(path);
    if (boost::beast::iequals(ext, ".htm"))  return "text/html";
    if (boost::beast::iequals(ext, ".html")) return "text/html";
    if (boost::beast::iequals(ext, ".php"))  return "text/html";
    if (boost::beast::iequals(ext, ".css"))  return "text/css";
    if (boost::beast::iequals(ext, ".txt"))  return "text/plain";
    if (boost::beast::iequals(ext, ".js"))   return "application/javascript";
    if (boost::beast::iequals(ext, ".json")) return "application/json";
    if (boost::beast::iequals(ext, ".xml"))  return "application/xml";
    if (boost::beast::iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if (boost::beast::iequals(ext, ".flv"))  return "video/x-flv";
    if (boost::beast::iequals(ext, ".png"))  return "image/png";
    if (boost::beast::iequals(ext, ".jpe"))  return "image/jpeg";
    if (boost::beast::iequals(ext, ".jpeg")) return "image/jpeg";
    if (boost::beast::iequals(ext, ".jpg"))  return "image/jpeg";
    if (boost::beast::iequals(ext, ".gif"))  return "image/gif";
    if (boost::beast::iequals(ext, ".bmp"))  return "image/bmp";
    if (boost::beast::iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if (boost::beast::iequals(ext, ".tiff")) return "image/tiff";
    if (boost::beast::iequals(ext, ".tif"))  return "image/tiff";
    if (boost::beast::iequals(ext, ".svg"))  return "image/svg+xml";
    if (boost::beast::iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
static std::string PathCat(const boost::beast::string_view& base, const boost::beast::string_view& path) {
    if (base.empty()) {
        return std::string(path);
    }
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if (result.back() == path_separator) {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());
    for (char& c : result) {
        if (c == '/') {
            c = path_separator;
        }
    }
#else
    char constexpr path_separator = '/';
    if (result.back() == path_separator) {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());
#endif
    return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<class Body, class Allocator, class Send>
static void HandleHttpRequest(boost::beast::string_view doc_root,
    boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator> >&& req,
    Send&& send)
{
    typedef typename boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator> > http_request_t;
    typedef boost::beast::http::response<boost::beast::http::string_body> http_response_t;
    struct Responses {

        // Returns a bad request response
        static http_response_t BadRequest(const http_request_t& req, boost::beast::string_view why) {
            http_response_t res{ boost::beast::http::status::bad_request, req.version() };
            res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        }

        // Returns a not found response
        static http_response_t NotFound(const http_request_t& req, boost::beast::string_view target) {
            http_response_t res{ boost::beast::http::status::not_found, req.version() };
            res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
        }

        // Returns a server error response
        static http_response_t ServerError(const http_request_t& req, boost::beast::string_view what) {
            http_response_t res{ boost::beast::http::status::internal_server_error, req.version() };
            res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(boost::beast::http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
        }
    };

    // Make sure we can handle the method
    if ((req.method() != boost::beast::http::verb::get) && (req.method() != boost::beast::http::verb::head)) {
        return send(Responses::BadRequest(req, "Unknown HTTP-method"));
    }

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || (req.target()[0] != '/') || (req.target().find("..") != boost::beast::string_view::npos)) {
        return send(Responses::BadRequest(req, "Illegal request-target"));
    }

    // Build the path to the requested file
    std::string path = PathCat(doc_root, req.target());
    if (req.target().back() == '/') {
        path.append("index.html");
    }

    // Attempt to open the file
    boost::beast::error_code ec;
    boost::beast::http::file_body::value_type body;
    body.open(path.c_str(), boost::beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == boost::beast::errc::no_such_file_or_directory) {
        return send(Responses::NotFound(req, req.target()));
    }

    // Handle an unknown error
    if (ec) {
        return send(Responses::ServerError(req, ec.message()));
    }

    // Cache the size since we need it after the move
    const uint64_t size = body.size();

    // Respond to HEAD request
    if (req.method() == boost::beast::http::verb::head) {
        boost::beast::http::response<boost::beast::http::empty_body> res{ boost::beast::http::status::ok, req.version() };
        res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(boost::beast::http::field::content_type, MimeType(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    boost::beast::http::response<boost::beast::http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(boost::beast::http::status::ok, req.version()) };
    res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(boost::beast::http::field::content_type, MimeType(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void PrintFail(boost::beast::error_code ec, char const* what) {
    // boost::asio::ssl::error::stream_truncated, also known as an SSL "short read",
    // indicates the peer closed the connection without performing the
    // required closing handshake (for example, Google does this to
    // improve performance). Generally this can be a security issue,
    // but if your communication protocol is self-terminated (as
    // it is with both HTTP and WebSocket) then you may simply
    // ignore the lack of close_notify.
    //
    // https://github.com/boostorg/beast/issues/38
    //
    // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
    //
    // When a short read would cut off the end of an HTTP message,
    // Beast returns the error boost::beast::http::error::partial_message.
    // Therefore, if we see a short read here, it has occurred
    // after the message has been completed, so it is safe to ignore it.

    if (ec == boost::asio::ssl::error::stream_truncated) {
        return;
    }

    LOG_ERROR(subprocess) << what << " (code=" << ec.value() << ") : " << ec.message() << "\n";
}

//------------------------------------------------------------------------------



// Echoes back all received WebSocket messages.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class websocket_session : public WebsocketSessionBase {
public:
    virtual ~websocket_session() override {} //for base shared_ptr
protected:
    explicit websocket_session(uint32_t uniqueId, ServerState_ptr& serverStatePtr) :
        WebsocketSessionBase(uniqueId),
        m_serverStatePtr(serverStatePtr),
        m_isOpenSharedLockPtr(boost::make_unique<shared_lock_t>(m_serverStatePtr->m_unclosedConnectionsSharedMutex)),
        m_writeInProgress(false),
        m_sendErrorOccurred(false) {}
private:
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    
    
    boost::beast::flat_buffer m_flatBuffer;
    ServerState_ptr m_serverStatePtr;
    std::queue<std::shared_ptr<std::string> > m_queueDataToSend;
    std::unique_ptr<shared_lock_t> m_isOpenSharedLockPtr;
    volatile bool m_writeInProgress;
    volatile bool m_sendErrorOccurred;

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void do_accept(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> req) {
        // Set suggested timeout settings for the websocket
        derived().GetWebsocketStream().set_option(
            boost::beast::websocket::stream_base::timeout::suggested(
                boost::beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        derived().GetWebsocketStream().set_option(
            boost::beast::websocket::stream_base::decorator(
                [](boost::beast::websocket::response_type& res) {
                    res.set(boost::beast::http::field::server, std::string(BOOST_BEAST_VERSION_STRING));
                }));

        // Accept the websocket handshake
        derived().GetWebsocketStream().async_accept(
            req,
            boost::bind(&websocket_session::on_accept,
                derived().shared_from_this(),
                boost::placeholders::_1));
    }

    void on_accept(boost::beast::error_code ec) {
        if (ec) {
            PrintFail(ec, "ws_accept");
            m_isOpenSharedLockPtr.reset();
        }
        else {
            // Read a message
            do_read();

            { //add the websocket connection only when fully running
                boost::mutex::scoped_lock lock(m_serverStatePtr->m_activeConnectionsMutex);
                std::pair<ServerState::active_connections_map_t::iterator, bool> ret =
                    m_serverStatePtr->m_activeConnections.emplace(m_uniqueId, derived().shared_from_this());
            }
            LOG_INFO(subprocess) << "Websocket connection id " << m_uniqueId << " connected.";

            if (m_serverStatePtr->m_onNewWebsocketConnectionCallback) {
                m_serverStatePtr->m_onNewWebsocketConnectionCallback(*this);
            }
        }
    }

    void do_read() {
        // Read a message into our buffer
        derived().GetWebsocketStream().async_read(
            m_flatBuffer,
            boost::bind(
                &websocket_session::on_read,
                derived().shared_from_this(),
                boost::placeholders::_1,
                boost::placeholders::_2));
    }

    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            if ((ec == boost::beast::websocket::error::closed) // This indicates that the websocket_session was closed (by remote)
                || (ec == boost::asio::error::connection_reset) // Connection reset by peer.
                || (ec == boost::asio::error::connection_aborted))
            {
                boost::mutex::scoped_lock lock(m_serverStatePtr->m_activeConnectionsMutex);
                if (m_serverStatePtr->m_activeConnections.erase(m_uniqueId) == 1) {
                    LOG_INFO(subprocess) << "Websocket connection id " << m_uniqueId << " closed by remote";
                }
            }
            else if (ec != boost::asio::error::operation_aborted) {
                PrintFail(ec, "ws_read");
            }

            m_isOpenSharedLockPtr.reset();
        }
        else {
            if (derived().GetWebsocketStream().got_text()) {
                if (m_serverStatePtr->m_onNewWebsocketDataReceivedCallback) {
                    m_serverStatePtr->m_onNewWebsocketDataReceivedCallback(*this, boost::beast::buffers_to_string(m_flatBuffer.data()));
                }
            }

            // Clear the buffer
            m_flatBuffer.consume(m_flatBuffer.size());

            // Do another read
            do_read();
        }
    }

    void DoSendQueuedElement() {
        derived().GetWebsocketStream().text(true); //set text mode
        derived().GetWebsocketStream().async_write(
            boost::asio::buffer(*m_queueDataToSend.front()),
            boost::bind(
                &websocket_session::OnSentStringFromQueuedSharedPtr,
                derived().shared_from_this(), //copied by value
                boost::placeholders::_1, boost::placeholders::_2));
    }

    void OnSentStringFromQueuedSharedPtr(boost::beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        m_queueDataToSend.pop();
        if (ec) {
            m_sendErrorOccurred = true;
            if (ec != boost::asio::error::operation_aborted) {
                PrintFail(ec, "ws_write");
            }
        }
        else if (m_queueDataToSend.empty()) {
            m_writeInProgress = false;
        }
        else {
            DoSendQueuedElement();
        }
    }

    void QueueAndSendTextData_NotThreadSafe(std::shared_ptr<std::string>& stringPtr) {
        if (!m_sendErrorOccurred) {
            m_queueDataToSend.push(std::move(stringPtr));
            if (!m_writeInProgress) {
                m_writeInProgress = true;
                DoSendQueuedElement();
            }
        }
    }

    void DoClose_NotThreadSafe() {
        if (m_isOpenSharedLockPtr) {
            derived().GetWebsocketStream().async_close(boost::beast::websocket::close_code::none,
                boost::bind(
                    &websocket_session::OnClose,
                    derived().shared_from_this(), //copied by value
                    boost::placeholders::_1)); 
        }
    }
    void OnClose(boost::beast::error_code ec) {
        if (ec) {
            PrintFail(ec, "ws_close");
        }
        m_isOpenSharedLockPtr.reset();
    }
public:
    // Start the asynchronous operation
    template<class Body, class Allocator>
    void run(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator> > req) {
        // Accept the WebSocket upgrade request
        do_accept(std::move(req));
    }
    virtual void AsyncSendTextData(std::shared_ptr<std::string>&& stringPtr) override {
        boost::asio::post(derived().GetWebsocketStream().get_executor(),
            boost::bind(&websocket_session::QueueAndSendTextData_NotThreadSafe,
                derived().shared_from_this(), std::move(stringPtr)));
    }

    virtual void AsyncClose() override {
        boost::asio::post(derived().GetWebsocketStream().get_executor(),
            boost::bind(&websocket_session::DoClose_NotThreadSafe,
                derived().shared_from_this()));
    }
};

//------------------------------------------------------------------------------

// Handles a plain WebSocket connection
class plain_websocket_session : public websocket_session<plain_websocket_session>,
    public std::enable_shared_from_this<plain_websocket_session>
{
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> m_websocketStream;

public:
    virtual ~plain_websocket_session() override {}
    // Create the session
    explicit plain_websocket_session(uint32_t uniqueId, boost::asio::ip::tcp::socket&& tcpSocket, ServerState_ptr& serverStatePtr) :
        websocket_session(uniqueId, serverStatePtr),
        m_websocketStream(std::move(tcpSocket)) {}

    // Called by the base class
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket>& GetWebsocketStream() {
        return m_websocketStream;
    }
};

//------------------------------------------------------------------------------

#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
// Handles an SSL WebSocket connection
class ssl_websocket_session : public websocket_session<ssl_websocket_session>,
    public std::enable_shared_from_this<ssl_websocket_session>
{
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket> > m_sslWebsocketStream;

public:
    virtual ~ssl_websocket_session() override {}
    // Create the ssl_websocket_session
    explicit ssl_websocket_session(uint32_t uniqueId, boost::beast::ssl_stream<boost::asio::ip::tcp::socket>&& sslStream, ServerState_ptr& serverStatePtr) :
        websocket_session(uniqueId, serverStatePtr),
        m_sslWebsocketStream(std::move(sslStream)) {}

    // Called by the base class
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket> >& GetWebsocketStream() {
        return m_sslWebsocketStream;
    }
};
#endif
//------------------------------------------------------------------------------


template<class Body, class Allocator>
void MakeWebsocketSession(boost::asio::ip::tcp::socket tcpSocket, ServerState_ptr& serverStatePtr,
    boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator> > req)
{
    const uint32_t uniqueId = serverStatePtr->m_nextWebsocketConnectionIdAtomic.fetch_add(1);
    std::shared_ptr<plain_websocket_session> session = std::make_shared<plain_websocket_session>(
        uniqueId, std::move(tcpSocket), serverStatePtr);
    session->run(std::move(req)); //creates shared_ptr copy
}

#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
template<class Body, class Allocator>
void MakeWebsocketSession(boost::beast::ssl_stream<boost::asio::ip::tcp::socket> sslStream, ServerState_ptr& serverStatePtr,
    boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator> > req)
{

    const uint32_t uniqueId = serverStatePtr->m_nextWebsocketConnectionIdAtomic.fetch_add(1);
    std::shared_ptr<ssl_websocket_session> session = std::make_shared<ssl_websocket_session>(
        uniqueId, std::move(sslStream), serverStatePtr);
    session->run(std::move(req)); //creates shared_ptr copy
}
#endif
//------------------------------------------------------------------------------

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class http_session {
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    // This queue is used for HTTP pipelining.
    class queue {
        static constexpr std::size_t QUEUE_LIMIT = 8; // Maximum number of responses we will queue

        // The type-erased, saved work item
        struct work {
            virtual ~work() = default;
            virtual void operator()() = 0;
        };

        http_session& m_selfRef;
        std::queue<std::unique_ptr<work> > m_queueInternal;

    public:
        explicit queue(http_session& self) : m_selfRef(self) {
            static_assert(QUEUE_LIMIT > 0, "queue limit must be positive");
        }

        // Returns `true` if we have reached the queue limit
        bool is_full() const noexcept {
            return m_queueInternal.size() >= QUEUE_LIMIT;
        }

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool on_write() {
            BOOST_ASSERT(!m_queueInternal.empty());
            const bool wasFull = is_full();
            m_queueInternal.pop();
            if (!m_queueInternal.empty()) {
                (*m_queueInternal.front())();
            }
            return wasFull;
        }

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void operator()(boost::beast::http::message<isRequest, Body, Fields>&& msg) {
            // This holds a work item
            struct work_impl : work
            {
                http_session& m_selfRef;
                boost::beast::http::message<isRequest, Body, Fields> m_httpMessage;

                work_impl(http_session& self, boost::beast::http::message<isRequest, Body, Fields>&& msg) :
                    m_selfRef(self),
                    m_httpMessage(std::move(msg)) {}

                void operator()() {
                    boost::beast::http::async_write(
                        m_selfRef.derived().stream(),
                        m_httpMessage,
                        boost::bind(
                            &http_session::on_write,
                            m_selfRef.derived().shared_from_this(),
                            m_httpMessage.need_eof(),
                            boost::placeholders::_1,
                            boost::placeholders::_2));
                }
            };

            // Allocate and store the work
            m_queueInternal.emplace(boost::make_unique<work_impl>(m_selfRef, std::move(msg)));

            // If there was no previous work, start this one
            if (m_queueInternal.size() == 1) {
                (*m_queueInternal.front())();
            }
        }
    };

    ServerState_ptr m_serverStatePtr;
    queue m_queue;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body> > m_requestParser;

protected:
    boost::beast::flat_buffer m_flatBuffer;

public:
    // Construct the session
    http_session(boost::beast::flat_buffer&& buffer, ServerState_ptr& serverStatePtr) :
        m_serverStatePtr(serverStatePtr),
        m_queue(*this),
        m_flatBuffer(std::move(buffer)) {}

    void do_read() {
        // Construct a new parser for each message
        m_requestParser.emplace();

        // Apply a reasonable limit to the allowed size
        // of the body in bytes to prevent abuse.
        m_requestParser->body_limit(10000);

        // Set the timeout.
        ////boost::beast::get_lowest_layer(derived().stream()).expires_after(std::chrono::seconds(30));

        // Read a request using the parser-oriented interface
        boost::beast::http::async_read(
            derived().stream(),
            m_flatBuffer,
            *m_requestParser,
            boost::bind(
                &http_session::on_read,
                derived().shared_from_this(),
                boost::placeholders::_1,
                boost::placeholders::_2));
    }

    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            if (ec == boost::beast::http::error::end_of_stream) { // This means they closed the connection
                derived().do_eof();
            }
            else if(ec != boost::beast::error::timeout) {
                PrintFail(ec, "http_read");
            }
        }
        else {
            // See if it is a WebSocket Upgrade
            if (boost::beast::websocket::is_upgrade(m_requestParser->get())) {
                // Disable the timeout.
                // The boost::beast::websocket::stream uses its own timeout settings.
                ////boost::beast::get_lowest_layer(derived().stream()).expires_never();

                // Create a websocket session, transferring ownership
                // of both the socket and the HTTP request.
                MakeWebsocketSession(derived().release_stream(), m_serverStatePtr, m_requestParser->release());
            }
            else {
                // Send the response
                HandleHttpRequest(m_serverStatePtr->m_docRoot, m_requestParser->release(), m_queue);

                // If we aren't at the queue limit, try to pipeline another request
                if (!m_queue.is_full()) {
                    do_read();
                }
            }
        }
    }

    void on_write(bool close, boost::beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            PrintFail(ec, "http_write");
            return;
        }

        if (close) {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            derived().do_eof();
            return;
        }

        // Inform the queue that a write completed
        if (m_queue.on_write()) {
            // Read another request
            do_read();
        }
    }
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session : public http_session<plain_http_session>,
    public std::enable_shared_from_this<plain_http_session>
{
    boost::asio::ip::tcp::socket m_tcpSocket;

public:
    // Create the session
    plain_http_session(boost::asio::ip::tcp::socket&& tcpSocket, boost::beast::flat_buffer&& buffer,
        ServerState_ptr& serverStatePtr) :
        http_session<plain_http_session>(std::move(buffer), serverStatePtr),
        m_tcpSocket(std::move(tcpSocket)) {}

    // Start the session
    void run() {
        this->do_read();
    }

    // Called by the base class
    boost::asio::ip::tcp::socket& stream() {
        return m_tcpSocket;
    }

    // Called by the base class
    boost::asio::ip::tcp::socket release_stream() {
        return std::move(m_tcpSocket);
    }

    // Called by the base class
    void do_eof() {
        // Send a TCP shutdown
        boost::beast::error_code ec;
        m_tcpSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
// Handles an SSL HTTP connection
class ssl_http_session : public http_session<ssl_http_session>,
    public std::enable_shared_from_this<ssl_http_session>
{
    boost::beast::ssl_stream<boost::asio::ip::tcp::socket> m_sslStream;

public:
    // Create the http_session
    ssl_http_session(boost::asio::ip::tcp::socket&& tcpSocket, boost::asio::ssl::context& ctx,
        boost::beast::flat_buffer&& buffer, ServerState_ptr& serverStatePtr) :
        http_session<ssl_http_session>(std::move(buffer), serverStatePtr),
        m_sslStream(std::move(tcpSocket), ctx) {}

    // Start the session
    void run() {
        // Set the timeout.
        ////boost::beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        // Note, this is the buffered version of the handshake.
        m_sslStream.async_handshake(
            boost::asio::ssl::stream_base::server,
            m_flatBuffer.data(),
            boost::bind(
                &ssl_http_session::on_handshake,
                shared_from_this(),
                boost::placeholders::_1,
                boost::placeholders::_2));
    }

    // Called by the base class
    boost::beast::ssl_stream<boost::asio::ip::tcp::socket>& stream() {
        return m_sslStream;
    }

    // Called by the base class
    boost::beast::ssl_stream<boost::asio::ip::tcp::socket> release_stream() {
        return std::move(m_sslStream);
    }

    // Called by the base class
    void do_eof() {
        // Set the timeout.
        ////boost::beast::get_lowest_layer(m_sslStream).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        m_sslStream.async_shutdown(
            boost::bind(
                &ssl_http_session::on_shutdown,
                shared_from_this(),
                boost::placeholders::_1));
    }

private:
    void on_handshake(boost::beast::error_code ec, std::size_t bytes_used) {
        if (ec) {
            PrintFail(ec, "ssl_http_handshake");
            return;
        }

        // Consume the portion of the buffer used by the handshake
        m_flatBuffer.consume(bytes_used);

        do_read();
    }

    void on_shutdown(boost::beast::error_code ec) {
        if (ec) {
            PrintFail(ec, "ssl_http_shutdown");
            return;
        }

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{
    boost::asio::ip::tcp::socket m_tcpSocket;
    boost::asio::ssl::context& m_sslContext;
    const bool m_sslContextIsValid;
    ServerState_ptr m_serverStatePtr;
    boost::beast::flat_buffer m_flatBuffer;

public:
    explicit detect_session(boost::asio::ip::tcp::socket&& socket,
        boost::asio::ssl::context& ctx, bool sslContextIsValid,
        ServerState_ptr& serverState) :
        m_tcpSocket(std::move(socket)),
        m_sslContext(ctx),
        m_sslContextIsValid(sslContextIsValid),
        m_serverStatePtr(serverState) {}

    // Launch the detector
    void run() {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        boost::asio::dispatch(
            m_tcpSocket.get_executor(),
            boost::bind(
                &detect_session::on_run,
                shared_from_this()));
    }

    void on_run() {
        // Set the timeout.
        ////m_tcpSocket.expires_after(std::chrono::seconds(30));

        boost::beast::async_detect_ssl(
            m_tcpSocket,
            m_flatBuffer,
            boost::bind(
                &detect_session::on_detect,
                shared_from_this(),
                boost::placeholders::_1,
                boost::placeholders::_2));
    }

    void on_detect(boost::beast::error_code ec, bool result) {
        if (ec) {
            PrintFail(ec, "detect");
            return;
        }

        if (result) {
            // Launch SSL session
            if (m_sslContextIsValid) {
                std::shared_ptr<ssl_http_session> session = std::make_shared<ssl_http_session>(std::move(m_tcpSocket), m_sslContext, std::move(m_flatBuffer), m_serverStatePtr);
                session->run();
            }
            else {
                LOG_ERROR(subprocess) << "Rejecting HTTPS session because SSL is not properly configured";
            }
        }
        else {
            // Launch plain session
            std::shared_ptr<plain_http_session> session = std::make_shared<plain_http_session>(std::move(m_tcpSocket), std::move(m_flatBuffer), m_serverStatePtr);
            session->run();
        }
    }
};
#endif //BEAST_WEBSOCKET_SERVER_SUPPORT_SSL

// Accepts incoming connections and launches the sessions
class listener {
    boost::asio::io_service& m_ioServiceRef;
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
    boost::asio::ssl::context& m_sslContext;
    const bool m_sslContextIsValid;
#endif
    boost::asio::ip::tcp::acceptor m_tcpAcceptor;
    ServerState_ptr m_serverStatePtr;

public:
    listener(boost::asio::io_service& ioService,
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
        boost::asio::ssl::context& ctx,
        bool sslContextIsValid,
#endif
        boost::asio::ip::tcp::endpoint endpoint,
        ServerState_ptr& serverStatePtr) :
        m_ioServiceRef(ioService),
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
        m_sslContext(ctx),
        m_sslContextIsValid(sslContextIsValid),
#endif
        m_tcpAcceptor(
#ifdef BEAST_WEBSOCKET_SERVER_SINGLE_THREADED
            ioService
#else
            boost::asio::make_strand(ioService)
#endif
        ),
        m_serverStatePtr(serverStatePtr)
    {
        boost::beast::error_code ec;

        // Open the acceptor
        m_tcpAcceptor.open(endpoint.protocol(), ec);
        if (ec) {
            PrintFail(ec, "listener_open");
            return;
        }

        // Allow address reuse
        m_tcpAcceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec) {
            PrintFail(ec, "listener_set_option");
            return;
        }

        // Bind to the server address
        m_tcpAcceptor.bind(endpoint, ec);
        if (ec) {
            PrintFail(ec, "listener_bind");
            return;
        }

        // Start listening for connections
        m_tcpAcceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            PrintFail(ec, "listen");
            return;
        }
    }

    ~listener() {
        if (m_tcpAcceptor.is_open()) {
            try {
                m_tcpAcceptor.close();
            }
            catch (const boost::system::system_error& e) {
                LOG_ERROR(subprocess) << "Error closing HTTP TCP Acceptor:  " << e.what();
            }
        }
    }

    // Start accepting incoming connections
    void run() {
        do_accept();
    }


private:
    void do_accept() {
        // The new connection gets its own strand
        std::shared_ptr<boost::asio::ip::tcp::socket> newTcpSocketPtr = std::make_shared<boost::asio::ip::tcp::socket>(
#ifdef BEAST_WEBSOCKET_SERVER_SINGLE_THREADED
            m_ioServiceRef
#else
            boost::asio::make_strand(m_ioServiceRef)
#endif
        );
        boost::asio::ip::tcp::socket& socketRef = *newTcpSocketPtr;
        m_tcpAcceptor.async_accept(
            socketRef,
            boost::bind(
                &listener::on_accept,
                this,
                std::move(newTcpSocketPtr),
                boost::placeholders::_1));
    }

    void on_accept(std::shared_ptr<boost::asio::ip::tcp::socket>& newTcpSocketPtr, boost::beast::error_code ec) {
        if (!ec) {
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
            // Create the detector http_session and run it
            std::shared_ptr<detect_session> detectSession = std::make_shared<detect_session>(
                std::move(*newTcpSocketPtr), m_sslContext, m_sslContextIsValid, m_serverStatePtr);
            detectSession->run();
#else
            // Launch plain session
            std::shared_ptr<plain_http_session> session = std::make_shared<plain_http_session>(
                std::move(*newTcpSocketPtr), boost::beast::flat_buffer(), m_serverStatePtr);
            session->run();
#endif
            // Accept another connection
            do_accept();
        }
        else if (ec != boost::asio::error::operation_aborted) {
            LOG_ERROR(subprocess) << "tcp accept error: " << ec.message();
        }
    }
};

struct BeastWebsocketServer::Impl : private boost::noncopyable {

    Impl(
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL 
        boost::asio::ssl::context&& sslContext, bool sslContextIsValid
#endif
    ) :
#ifdef BEAST_WEBSOCKET_SERVER_SINGLE_THREADED
        m_ioService()
#else
        m_ioService(BEAST_WEBSOCKET_SERVER_NUM_THREADS) //(1) => concurrency_hint of 1 thread
#endif // !BEAST_WEBSOCKET_SERVER_SINGLE_THREADED
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
        ,
        m_sslContext(std::move(sslContext)),
        m_sslContextIsValid(sslContextIsValid)
#endif
    {}
    ~Impl() {
        Stop();
    }
    void Stop() {
        m_listenerUniquePtr.reset(); //stop future connections
        if (m_serverStatePtr) {
            boost::mutex::scoped_lock lock(m_serverStatePtr->m_activeConnectionsMutex);
            for (ServerState::active_connections_map_t::iterator it = m_serverStatePtr->m_activeConnections.begin();
                it != m_serverStatePtr->m_activeConnections.end(); ++it)
            {
                it->second->AsyncClose();
            }

            //Clear this map's collection of shared_ptrs.
            //The websocket connections themselves however contain their own copy of these shared_ptrs.
            m_serverStatePtr->m_activeConnections.clear(); 

            { //wait for websockets to gracefully close
                exclusive_lock_t lockExclusive(m_serverStatePtr->m_unclosedConnectionsSharedMutex);
            }
            m_serverStatePtr.reset();
        }
        
        if (m_ioServiceThreadPtrs[0]) {
            m_ioService.stop(); //stop anything remaining
            for (std::size_t i = 0; i < m_ioServiceThreadPtrs.size(); ++i) {
                try {
                    m_ioServiceThreadPtrs[i]->join();
                    m_ioServiceThreadPtrs[i].reset(); //delete it
                }
                catch (const boost::thread_resource_error&) {
                    LOG_ERROR(subprocess) << "error stopping BeastWebsocketServer io_service";
                }
            }
        }
    }
    bool Init(const boost::filesystem::path& documentRoot, const std::string& portNumberAsString,
        const OnNewBeastWebsocketConnectionCallback_t& connectionCallback, const OnNewBeastWebsocketDataReceivedCallback_t& dataCallback)
    {
        const boost::asio::ip::address address = boost::asio::ip::make_address("0.0.0.0");
        const uint16_t port = boost::lexical_cast<uint16_t>(portNumberAsString);
        m_serverStatePtr = std::make_shared<ServerState>(documentRoot.string(), connectionCallback, dataCallback);
        // Create and launch a listening port
        m_listenerUniquePtr = boost::make_unique<listener>(m_ioService,
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
            m_sslContext, m_sslContextIsValid,
#endif
            boost::asio::ip::tcp::endpoint(address, port), m_serverStatePtr);
        m_listenerUniquePtr->run();

        //StartTcpAccept();
#ifdef BEAST_WEBSOCKET_SERVER_SINGLE_THREADED
        m_ioServiceThreadPtrs[0] = boost::make_unique<boost::thread>(boost::bind(&boost::asio::io_service::run, &m_ioService));
        ThreadNamer::SetIoServiceThreadName(m_ioService, "ioServiceWebserver");
#else
        for (std::size_t i = 0; i < m_ioServiceThreadPtrs.size(); ++i) {
            m_ioServiceThreadPtrs[i] = boost::make_unique<boost::thread>([this, i]() {
                static const std::string namePrefix("ioServiceWeb");
                ThreadNamer::SetThisThreadName(namePrefix + boost::lexical_cast<std::string>(i));
                m_ioService.run();
            });
        }
#endif

        LOG_INFO(subprocess) << "HDTN Webserver at http://localhost:" << portNumberAsString;

        return true;
    }
    void SendTextDataToActiveWebsockets(const std::shared_ptr<std::string>& stringPtr) {
        boost::mutex::scoped_lock lock(m_serverStatePtr->m_activeConnectionsMutex);
        for (ServerState::active_connections_map_t::iterator it = m_serverStatePtr->m_activeConnections.begin();
            it != m_serverStatePtr->m_activeConnections.end(); ++it)
        {
            it->second->AsyncSendTextData(std::shared_ptr<std::string>(stringPtr));
        }
    }

private:
    boost::asio::io_service m_ioService;
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
    // The SSL context is required, and holds certificates
    boost::asio::ssl::context m_sslContext;
    const bool m_sslContextIsValid;
#endif

    //must be shared since listener inherits from enable_shared_from_this
    // (actually not needed, ptr contained within its callbacks)
    //std::shared_ptr<listener> m_listenerSharedPtr; 

    std::array<std::unique_ptr<boost::thread>, BEAST_WEBSOCKET_SERVER_NUM_THREADS> m_ioServiceThreadPtrs;
    std::unique_ptr<listener> m_listenerUniquePtr;
    ServerState_ptr m_serverStatePtr;
public:
    
};


///////////////////////////
//public facing functions
///////////////////////////
BeastWebsocketServer::BeastWebsocketServer(
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
    boost::asio::ssl::context&& sslContext, bool sslContextIsValid
#endif
) :
    m_pimpl(boost::make_unique<BeastWebsocketServer::Impl>(
#ifdef BEAST_WEBSOCKET_SERVER_SUPPORT_SSL
        std::move(sslContext), sslContextIsValid
#endif
        )) {}
BeastWebsocketServer::~BeastWebsocketServer() {
    Stop();
}
bool BeastWebsocketServer::Init(const boost::filesystem::path& documentRoot, const std::string& portNumberAsString,
    const OnNewBeastWebsocketConnectionCallback_t& connectionCallback, const OnNewBeastWebsocketDataReceivedCallback_t& dataCallback)
{
    return m_pimpl->Init(documentRoot, portNumberAsString, connectionCallback, dataCallback);
}
void BeastWebsocketServer::Stop() {
    m_pimpl->Stop();
}
void BeastWebsocketServer::SendTextDataToActiveWebsockets(const std::shared_ptr<std::string>& stringPtr) {
    m_pimpl->SendTextDataToActiveWebsockets(stringPtr);
}
