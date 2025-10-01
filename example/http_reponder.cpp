#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <boost/asio.hpp> // Needed for tcp::socket and boost::asio::write

#include "pipef.h" // Presumed custom pipeline library

using namespace std;
using tcp = boost::asio::ip::tcp;

// Reads the full content of an HTML file
std::string read_html_file(const std::filesystem::path& file_path) {
    std::ifstream file(file_path);
    if (!file) {
        throw std::runtime_error("Failed to open HTML file: " + file_path.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Handles incoming HTTP request (for now, just logs and passes through)
std::string handle_request(const std::string& request) {
    std::cout << "[INFO] Received HTTP request:\n" << request << "\n";
    return request;
}

// Generates a simple HTTP 200 OK response with the given HTML content
std::string generate_response(const std::string& html_content) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html\r\n"
             << "Content-Length: " << html_content.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << html_content;
    return response.str();
}

// Sends the HTTP response to the client
void send_response(const std::shared_ptr<tcp::socket>& socket, const std::string& response) {
    try {
        boost::asio::write(*socket, boost::asio::buffer(response));
        socket->shutdown(tcp::socket::shutdown_both);
        socket->close();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to send response: " << e.what() << "\n";
    }
}

int main() {
    try {
        const std::filesystem::path html_file_path = "index.html";
        const unsigned short port = 8000;

        const std::string html_content = read_html_file(html_file_path);
        std::cout << "[INFO] Loaded HTML content from: " << html_file_path << "\n";

        // Create the pipeline engine
        auto engine = pipef::engine::create();

        // Build each pipeline component
        auto request_source = engine->create<tcp_input_source>(port);
        auto request_handler = engine->create<transformer<std::string>>(handle_request);
        auto response_creator = engine->create<transformer<std::string>>(
            [html_content](const std::string&) { return generate_response(html_content); });
        auto response_sender = engine->create<tcp_output_sink>();

        // Assemble the pipeline
        *request_source
            | *request_handler
            | *response_creator
            | *response_sender;

        std::cout << "[INFO] HTTP server running on port " << port << "\n";

        // Run indefinitely
        constexpr int loop_forever = INFINITE;
        constexpr int interval_ms = 100;
        engine->run(loop_forever, interval_ms);

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Server failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
