
int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto help_filter = engine->create<character_filter>();
    auto command_mapper = engine->create<command_map>();
    auto sink = engine->create<print_sink>();
    auto src_cnter = engine->create<input_counter>();
    auto help_fltr_cnter = engine->create<input_counter>();

    src | sink[stdout];
    src | help_filter["help"] | map[[](data_uptr d){ return std::string("Help string... %s", d->to_string()); }] | sink[stdout];
    src | command_mapper["history"].set(hist_cmd_func);
    src | command_mapper["quit"].set(exit);
    src | src_cnter;
    help_filter | help_fltr_cnter;

    engine->run(INFINITE /* loop count */, INFINITE /* duraion ms */);
    
    std::cout << "End of program." << std:endl;
    std::cout << "Count of key:" << src_cnter->get() << std:endl;
    std::cout << "Count of filter:" << fltr_cnter->get() << std:endl;
    std::cout << "Count of help:" << help_fltr_cnter->get() << std:endl;

    return 0;
}
