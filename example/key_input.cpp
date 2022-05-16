import pipef

int main(int argc, char** argv)
{
    auto engine = make_unique<pipef::engine>(pipef::engine::create());
    auto src = engine.create<key_input_src>();
    auto filter = engine.create<character_filter>();
    auto sink = engine.create<print_sink>();
    
    filter->set_blist("abcd");

    src | filter | sink;

    engine.run(10 /* loop count */, 10000 /* duraion ms */);
    
    cout << "End of program." << src.get() << sink.get();

    return 0;
}
