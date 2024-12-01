#include <iostream>
#include <memory>
#include <string>
#include <fstream>
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

// Pipeline step: Read HTTP request
std::string handle_request(const std::string& request) {
    std::cout << "Received HTTP request:\n" << request << std::endl;
    return request; // No special handling here, but can be extended
}

// Pipeline step: Generate an HTTP response with the HTML content
std::string generate_response(const std::string& html_content) {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/html\r\n";
    response << "Content-Length: " << html_content.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << html_content;
    return response.str();
}

// Function to send the HTTP response over the socket
void send_response(std::shared_ptr<tcp::socket> socket, const std::string& response) {
    boost::asio::write(*socket, boost::asio::buffer(response));
    socket->close();
}

int main() {
    try {
        const std::string html_file = "index.html"; // HTML file to serve
        const unsigned short port = 8080;          // Port to listen on

        // Read the HTML content
        std::string html_content = read_html_file(html_file);

        // Create engine and pipeline components
        auto engine = pipef::engine::create();
        auto request_source = engine->create<tcp_input_source>(port);
        auto request_processor = engine->create<transformer<std::string>>(handle_request);
        auto response_generator = engine->create<transformer<std::string>>(
            [html_content](const std::string&) { return generate_response(html_content); });
        auto response_sender = engine->create<tcp_output_sink>();

        // Build the pipeline
        request_source
            | request_processor
            | response_generator
            | response_sender;

        // Run the pipeline
        engine->run(INFINITE /* loop count */, 10000 /* duration ms */);

        std::cout << "HTTP server is running on port " << port << "..." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
