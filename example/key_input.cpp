#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <atomic> // For a more robust exit flag

// Assume these are provided by your library
namespace pipef {
    namespace engine {
        // Mock classes for demonstration
        class engine {
        public:
            static std::unique_ptr<engine> create() { return std::make_unique<engine>(); }
            template<typename T>
            auto create() { return std::make_unique<T>(); }
            void run(int loop_count, int duration) {
                // Mock run logic
                std::cout << "Engine running for " << duration << "ms..." << std::endl;
            }
        };
    }
}
class key_input_src {};
class character_filter {};
class command_map {
public:
    command_map& set(std::function<void(const std::string&)>) { return *this; }
};
class print_sink {
public:
    void operator[](std::ostream&) {}
};
struct data_uptr {
    std::string to_string() { return "data content"; }
};
// End of assumed library components

// Use named constants for clarity
const int RUN_LOOP_COUNT_INFINITE = -1; // Or whatever value INFINITE represents
const int RUN_DURATION_MS = 10000;

// Flag for graceful exit
std::atomic<bool> g_quit = false;

// Function to run CLI commands
void run_cli_cmd(const std::string& command) {
    std::cout << "Executing command: " << command << std::endl;
}

// Function to handle the "history" command
void history_command(const std::string&) {
    std::cout << "History command executed." << std::endl;
}

// Function to display help information
std::string generate_help_string(data_uptr data) {
    return "Help string... " + data.to_string();
}

int main() {
    try {
        // Create engine and pipeline components
        auto engine = pipef::engine::create();
        auto src = engine->create<key_input_src>();
        auto help_filter = engine->create<character_filter>();
        auto command_mapper = engine->create<command_map>();
        auto sink = engine->create<print_sink>();

        // Setup pipeline
        // The original logic `src | sink[stdout];` seems to be a custom operator.
        // I will assume it's valid and keep it.
        src | sink[std::cout];

        src 
            | help_filter["help"]
            | [&](data_uptr d) { return generate_help_string(std::move(d)); } // Use a lambda for the filter
            | sink[std::cout];

        src | command_mapper["history"].set(history_command);
        
        // Use a lambda for the quit command to set the flag
        src | command_mapper["quit"].set([](const std::string&) {
            std::cout << "Quit command received." << std::endl;
            g_quit = true;
        });
        
        src | command_mapper["run"].set(run_cli_cmd);

        // Run the engine
        // Loop and check the quit flag.
        while (!g_quit) {
            engine->run(1, RUN_DURATION_MS); // Run for a short duration and check the flag
        }

        std::cout << "End of program." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred." << std::endl;
        return 1;
    }

    return 0;
}
