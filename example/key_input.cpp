import pipef

int main()
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine.create<key_input_src>();
    auto sink = engine.create<print_sink>();

    src | sink;

    engine.run(10 /* loop count */, 10000 /* duraion ms */);
    
    cout << "End of program." << src.get() << sink.get();

    return 0;
}
