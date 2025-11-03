#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace pipef {
namespace engine {
class engine {
 public:
  static std::unique_ptr<engine> create() { return std::make_unique<engine>(); }

  template <typename T>
  std::unique_ptr<T> create() {
    return std::make_unique<T>();
  }

  void run(int loop_count, int duration_ms) {
    // Simulates engine work for the given duration.
  }
};
}  // namespace engine
}  // namespace pipef

class key_input_src {};
class character_filter {
 public:
  character_filter& operator[](const std::string&) { return *this; }
  template <typename Func>
  character_filter& operator|(Func&& f) {
    return *this;
  }
};
class command_map {
 public:
  command_map& operator[](const std::string&) { return *this; }
  command_map& set(std::function<void(const std::string&)>) { return *this; }
};
class print_sink {
 public:
  void operator[](std::ostream&) {}
};
struct data_uptr {
  std::string to_string() { return "data content"; }
};
// -----------------------------------------------------------

namespace {
// Constants are grouped and placed in an anonymous namespace.
constexpr int kStepDurationMs = 100;

// Using constexpr for command strings avoids "magic strings".
namespace Command {
constexpr char kHelp[] = "help";
constexpr char kHistory[] = "history";
constexpr char kRun[] = "run";
constexpr char kQuit[] = "quit";
}  // namespace Command

}  // namespace

/// Groups command handlers for better organization.
namespace CliCommands {

void run_cli_cmd(const std::string& command) {
  std::cout << "[Command] Executing: " << command << std::endl;
}

void handle_history(const std::string& /*unused*/) {
  std::cout << "[Command] Showing command history." << std::endl;
}

std::string make_help_string(data_uptr data) {
  return "Help: " + data->to_string();
}

}  // namespace CliCommands

/**
 * @class Application
 * @brief Encapsulates the application's state and main loop.
 *
 * This class removes the need for global variables like 'g_should_quit'
 * by holding the application state internally. This improves structure and
 * makes the code more testable and easier to reason about.
 */
class Application {
 public:
  void run() {
    try {
      setup_pipeline();
      main_loop();
    } catch (const std::exception& e) {
      std::cerr << "[Fatal Error] " << e.what() << std::endl;
      // Consider specific cleanup if necessary
    } catch (...) {
      std::cerr << "[Fatal Error] Unknown exception occurred." << std::endl;
    }
  }

 private:
  void setup_pipeline() {
    // Create pipeline nodes using the engine.
    auto input = engine_->create<key_input_src>();
    auto help_filter = engine_->create<character_filter>();
    auto commands = engine_->create<command_map>();
    auto output = engine_->create<print_sink>();

    // The pipeline defines how data flows from input to output.
    *input | (*output)[std::cout];

    *input | (*help_filter)[Command::kHelp] |
        [](data_uptr d) { return CliCommands::make_help_string(std::move(d)); } |
        (*output)[std::cout];

    // Link commands to their respective handlers.
    *input | (*commands)[Command::kHistory].set(CliCommands::handle_history);
    *input | (*commands)[Command::kRun].set(CliCommands::run_cli_cmd);
    
    // The quit command lambda now captures 'this' to modify the member flag.
    *input | (*commands)[Command::kQuit].set([this](const std::string&) {
      std::cout << "[Command] Quit received.\n";
      should_quit_.store(true, std::memory_order_release);
    });

    std::cout << "[System] CLI initialized. Waiting for input..." << std::endl;
  }

  void main_loop() {
    // The main loop now checks the internal atomic flag.
    while (!should_quit_.load(std::memory_order_acquire)) {
      engine_->run(1, kStepDurationMs);
    }
    std::cout << "[System] Program terminated.\n";
  }

  std::unique_ptr<pipef::engine::engine> engine_ = pipef::engine::create();
  std::atomic<bool> should_quit_{false};
};

int main() {
  Application app;
  app.run();
  return 0;
}
