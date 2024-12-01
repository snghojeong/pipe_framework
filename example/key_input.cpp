#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include "pipef.h" // Assuming this is your library for pipef framework

// Function to run CLI commands
void run_cli_cmd(const std::string& command) {
    std::cout << "Execute command: " << command << std::endl;
}

// Function to exit the program
void quit_program() {
    std::cout << "Exiting program." << std::endl;
    std::exit(0);
}

int main() {
    // Create engine
    auto engine = pipef::engine::create(); // Assuming `create()` returns a unique_ptr
    auto src = engine->create<key_input_src>();
    auto help_filter = engine->create<character_filter>();
    auto command_mapper = engine->create<command_map>();
    auto sink = engine->create<print_sink>();

    // Pipeline setup
    src | sink[stdout];
    src 
        | help_filter["help"] 
        | map([](data_uptr d) { 
              return "Help string... " + d->to_string(); 
          }) 
        | sink[stdout];

    // Command mappings
    src | command_mapper["history"].set([](const std::string&) {
        std::cout << "History command executed." << std::endl;
    });
    src | command_mapper["quit"].set(quit_program);
    src | command_mapper["run"].set(run_cli_cmd);

    // Run the engine
    engine->run(INFINITE /* loop count */, 10000 /* duration ms */);

    std::cout << "End of program." << std::endl;

    return 0;
}
