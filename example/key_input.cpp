#include <iostream>
#include <memory>
#include <string>
#include <functional>

// Function to run CLI commands
void run_cli_cmd(const std::string& command) {
    std::cout << "Executing command: " << command << std::endl;
}

// Function to exit the program
void quit_program() {
    std::cout << "Exiting program." << std::endl;
    std::exit(0);
}

// Function to handle the "history" command
void history_command(const std::string&) {
    std::cout << "History command executed." << std::endl;
}

// Function to display help information
std::string generate_help_string(data_uptr data) {
    return "Help string... " + data->to_string();
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
        src | sink[stdout];

        src 
            | help_filter["help"]
            | map(generate_help_string) 
            | sink[stdout];

        src | command_mapper["history"].set(history_command);
        src | command_mapper["quit"].set(quit_program);
        src | command_mapper["run"].set(run_cli_cmd);

        // Run the engine
        engine->run(INFINITE /* loop count */, 10000 /* duration ms */);

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
