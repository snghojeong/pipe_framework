#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "pipef.h" // Assuming this is the custom library for pipeline processing

// Function to read an HTML file from disk
std::string read_html_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Pipeline step: Process HTTP request
std::string handle_request(const std::string& request) {
    std::cout << "Received HTTP request:\n" << request << std::endl;
    return request; // Can be extended to parse or process the request further
}

// Pipeline step: Generate an HTTP response
std::string generate_response(const std::string& html_content) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html\r\n"
             << "Content-Length: " << html_content.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << html_content;
    return response.str();
}

// Function to send an HTTP response
void send_response(std::shared_ptr<tcp::socket> socket, const std::string& response) {
    try {
        boost::asio::write(*socket, boost::asio::buffer(response));
        socket->close();
    } catch (const std::exception& e) {
        std::cerr << "Error sending response: " << e.what() << std::endl;
    }
}

int main() {
    try {
        const std::string html_file = "index.html"; // Path to the HTML file
        const unsigned short port = 8080;          // Port to listen on

        // Read the HTML content from file
        const std::string html_content = read_html_file(html_file);

        // Create engine and pipeline components
        auto engine = pipef::engine::create();
        auto request_source = engine->create<tcp_input_source>(port);
        auto request_processor = engine->create<transformer<std::string>>(handle_request);
        auto response_generator = engine->create<transformer<std::string>>(
            [html_content](const std::string&) { return generate_response(html_content); });
        auto response_sender = engine->create<tcp_output_sink>();

        // Build the pipeline
        *request_source
            | *request_processor
            | *response_generator
            | *response_sender;

        // Run the pipeline
        constexpr int loop_count = INFINITE; // Unlimited loop count
        constexpr int duration_ms = 10000;  // Duration in milliseconds
        engine->run(loop_count, duration_ms);

        std::cout << "HTTP server is running on port " << port << "..." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
