
void exit() 
{
    exit(0);
}

int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine->create<key_input_src>();
    auto help_filter = engine->create<character_filter>();
    auto command_mapper = engine->create<command_map>();
    auto sink = engine->create<print_sink>();

    src | sink[stdout];
    src | help_filter["help"] | map[[](data_uptr d){ return std::string("Help string... %s", d->to_string()); }] | sink[stdout];
    src | command_mapper["history"].set(hist_cmd_func);
    src | command_mapper["quit"].set(exit);

    engine->run(INFINITE /* loop count */, 10000 /* duraion ms */);
    
    std::cout << "End of program." << std:endl;

    return 0;
}
