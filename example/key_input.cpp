
int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto filter = engine->create<character_filter>();
    auto sink = engine->create<print_sink>();
    auto map = engine->create<lambda_map>();

    src | filter["*"] | sink[stdout];
          filter["&"] | sink[stderr];
    src | map[[](data_uptr dat){ std::cout << dat->get() << std::endl; }] | sink[stdout];
    src | filter["help"] | map[[](data_uptr dat){ return std::string("Help string..."); }] | sink[stdout];

    engine->run(INFINITE /* loop count */, INFINITE /* duraion ms */);
    
    std::cout << "End of program." << std:endl;
    std::cout << "Count of key:" << src->get() << std:endl;
    std::cout << "Count of print:" << sink->get() << std:endl;

    return 0;
}
