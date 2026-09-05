#include "hbfsim/core.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: hbfsim <config.yaml> <trace.csv>\n";
    return 2;
  }
  try {
    auto config = hbfsim::Config::from_yaml_file(argv[1]);
    const auto trace = hbfsim::TraceReader::read_csv(argv[2]);
    hbfsim::Simulator simulator(config);
    for (const auto& entry : trace) simulator.submit(entry);
    simulator.run();
    simulator.stats().write(config.output_dir, simulator.now());
    std::cout << "Completed " << simulator.stats().completed_requests()
              << " requests (" << simulator.stats().failed_requests()
              << " failed); mean latency "
              << simulator.stats().mean_latency_ns() << " ns; p99 "
              << simulator.stats().p99_latency_ns() << " ns\nResults: " << config.output_dir << '\n';
  } catch (const std::exception& error) {
    std::cerr << "hbfsim: " << error.what() << '\n';
    return 1;
  }
}
