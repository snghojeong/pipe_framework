
int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto filter = engine->create<character_filter>();
    auto help_filter = engine->create<character_filter>();
    auto sink = engine->create<print_sink>();
    auto map = engine->create<lambda_map>();
    auto src_cnter = engine->create<input_counter>();
    auto fltr_cnter = engine->create<input_counter>();
    auto help_fltr_cnter = engine->create<input_counter>();

    src | filter["*"] | sink[stdout];
    src | map[[](data_uptr dat){ std::cout << dat->get() << std::endl; }] | sink[stdout];
    src | help_filter["help"] | map[[](data_uptr d){ return std::string("Help string..."); }] | sink[stdout];
    src | src_cnter;
    filter | fltr_cnter;
    help_filter | help_fltr_cnter;

    engine->run(INFINITE /* loop count */, INFINITE /* duraion ms */);
    
    std::cout << "End of program." << std:endl;
    std::cout << "Count of key:" << src_cnter->get() << std:endl;
    std::cout << "Count of filter:" << fltr_cnter->get() << std:endl;
    std::cout << "Count of help:" << help_fltr_cnter->get() << std:endl;

    return 0;
}
