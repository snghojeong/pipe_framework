import pipef

int main()
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine.create<key_input_src>();
    auto sink = engine.create<print_sink>();

    src | sink;

    engine.run(10 /* loop count */, 10000 /* duraion ms */);
    
    printf("End of program.");

    return 0;
}
