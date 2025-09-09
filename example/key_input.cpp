#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

// ----------------- Assumed Framework Types -----------------
namespace pipef {
    namespace engine {
        class engine {
        public:
            static std::unique_ptr<engine> create() { return std::make_unique<engine>(); }

            template<typename T>
            std::unique_ptr<T> create() { return std::make_unique<T>(); }

            void run(int loop_count, int duration_ms) {
                std::cout << "[Engine] Running loop for " << duration_ms << "ms...\n";
            }
        };
    }
}

class key_input_src {};
class character_filter {
public:
    character_filter& operator[](const std::string&) { return *this; }
    template <typename Func>
    character_filter& operator|(Func&& f) { return *this; }
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

// Constants
constexpr int kLoopForever = -1;
constexpr int kStepDurationMs = 1000;

// Global exit flag
std::atomic<bool> g_should_quit = false;

// Command logic
void run_cli_cmd(const std::string& command) {
    std::cout << "[Command] Executing: " << command << '\n';
}

void handle_history(const std::string&) {
    std::cout << "[Command] Showing command history.\n";
}

std::string make_help_string(data_uptr data) {
    return "Help: " + data.to_string();
}

int main() {
    try {
        auto engine = pipef::engine::create();

        // Create pipeline nodes
        auto input = engine->create<key_input_src>();
        auto help_filter = engine->create<character_filter>();
        auto commands = engine->create<command_map>();
        auto output = engine->create<print_sink>();

        // Build pipeline
        input | output[std::cout];

        input
            | help_filter["help"]
            | [](data_uptr d) { return make_help_string(std::move(d)); }
            | output[std::cout];

        input | commands["history"].set(handle_history);
        input | commands["run"].set(run_cli_cmd);
        input | commands["quit"].set([](const std::string&) {
            std::cout << "[Command] Quit received.\n";
            g_should_quit.store(true, std::memory_order_release);
        });

        std::cout << "[System] CLI initialized. Waiting for input...\n";

        // Main loop with graceful shutdown check
        while (!g_should_quit.load(std::memory_order_acquire)) {
            engine->run(1, kStepDurationMs);
        }

        std::cout << "[System] Program terminated.\n";
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << '\n';
    } catch (...) {
        std::cerr << "[Fatal Error] Unknown exception occurred.\n";
    }

    return EXIT_FAILURE;
}
