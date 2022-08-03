
int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto filter = engine->create<character_filter>();
    auto sink = engine->create<print_sink>();

    src | filter["*"] | sink[stdout];
          filter["&"] | sink[stderr];
          map([](data_uptr dat){ std::cout << dat->get() << std::endl; }) | sink[stdout];

    engine->run(LOOP_INFINITE /* loop count */, LOOP_INFINITE /* duraion ms */);
    
    std::cout << "End of program." << src->get() << sink->get();

    return 0;
}
