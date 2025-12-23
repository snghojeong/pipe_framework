#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <boost/asio.hpp>

#include <pipef.h>

std::string read_file_content(std::string_view file_path) {
    std::ifstream file(file_path.data());
    if (!file) {
        // Use std::ostringstream to build complex error messages safely.
        std::ostringstream oss;
        oss << "Failed to open HTML file: " << file_path;
        throw std::runtime_error(oss.str());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string return_echo(std::string input) {
    return input;
}

// Renamed for clarity. This function only logs the request, it doesn't "handle" it.
// Use [[maybe_unused]] to signal that the parameter is intentionally not used.
std::string log_request([[maybe_unused]] const std::string& request) {
    // std::clog is more appropriate for logging than std::cout.
    std::clog << "[INFO] Received new HTTP request.\n";
    // For brevity, we don't log the full request here, but you could add it back:
    // std::clog << request << "\n";
    return request; // Pass the request through to the next pipeline stage.
}

// Generates a simple HTTP 200 OK response with the given HTML content.
std::string generate_response(std::string_view html_content) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html\r\n"
             << "Content-Length: " << html_content.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << html_content;
    return response.str();
}

int main() {
    // Use constexpr for true compile-time constants.
    constexpr unsigned short port = 8000;
    constexpr std::string_view html_file_path = "index.html";
    
    // Define engine run parameters as constants for clarity.
    // -1 is often used to signify "run forever" in such libraries.
    constexpr int run_forever = -1; 
    constexpr int poll_interval_ms = 10;

    try {
        const std::string html_content = http_server::read_file_content(html_file_path);
        std::clog << "[INFO] Loaded HTML content from: " << html_file_path << "\n";

        // Create the pipeline engine
        auto engine = pipef::engine::create();

        // Build each pipeline component with clearer variable names
        auto source    = engine->create<tcp_input_source>(port);
        auto logger    = engine->create<transformer<std::string>>(http_server::log_request);
        
        // The responder ignores the incoming data and always serves the same page.
        // Capture html_content by reference (&) to avoid copying the large string.
        auto responder = engine->create<transformer<std::string>>(
            [&html_content]([[maybe_unused]] const std::string& request) {
                return http_server::generate_response(html_content);
            });
            
        auto sink      = engine->create<tcp_output_sink>();

        *source | *logger | *responder | *sink;

        std::clog << "[INFO] HTTP server running on port " << port << "\n";

        // Run the pipeline engine
        engine->run(run_forever, poll_interval_ms);

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Server failed: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
