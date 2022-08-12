
int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto filter = engine->create<character_filter>();
    auto sink = engine->create<print_sink>();

    src | filter["*"] | sink[stdout];
          filter["&"] | sink[stderr];
          map([](data_uptr dat){ std::cout << dat->get() << std::endl; }) | sink[stdout];

    engine->run(INFINITE /* loop count */, 10000 /* duraion ms */);
    
    std::cout << "End of program." << std:endl;
    std::cout << "Count of key:" << src->get() << std:endl;
    std::cout << "Count of print:" << sink->get() << std:endl;

    return 0;
}
