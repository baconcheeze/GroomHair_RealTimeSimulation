namespace gpuls
{
int hybridDynamics(Context &c, const fs::path &inputs, const fs::path &out)
{
    Engine engine(c, 256 * 1024, 128 * 1024);
    hybridEngine = &engine;
    hybridRecords.clear();
    D gravity{-9.699665069580078, .06558697670698166, -1.4657117128372192};
    bool pass = true;
    for (auto name : {"primary", "heldout-slot0", "heldout-slot2"})
    {
        auto p = jointcpu::loadProblem(inputs / (std::string(name) + ".jwp"));
        auto trajectory = simulate(*p, 16, gravity, D{}, false);
        pass &= trajectory.pass;
        write(out / (std::string(name) + ".json"), pointsJSON(trajectory));
    }
    std::ostringstream o;
    o << "{\"pass\":" << (pass ? "true" : "false")
      << ",\"substepsPerStrand\":16,\"strands\":3,\"hybridLinearSolves\":" << hybridRecords.size() << ",\"correctionsPerLinearSolve\":"<<activeLaunch.corrections<< '}';
    write(out / "STATUS.json", o.str());
    return pass ? 0 : 1;
}
} // namespace gpuls
