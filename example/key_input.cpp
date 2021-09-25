
int main()
{
    auto engine = pipef::engine::create();
    auto src = engine.create<key_input_src>();
    auto sink = engine.create<print_sink>();

    src >> sink;

    engine.run();

    return 0;
}
