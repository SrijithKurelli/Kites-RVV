#include "profiler/vector_timing_model.h"

#include <charconv>
#include <cstdlib>
#include <system_error>
#include <iomanip>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Kites::Performance::TimingConfig;
using Kites::Performance::TimingInstruction;
using Kites::Performance::TimingOpKind;
using Kites::Performance::TimingSummary;
using Kites::Performance::VectorTimingModel;

namespace
{
struct KernelPair
{
    std::string name;
    std::vector<TimingInstruction> scalar;
    std::vector<TimingInstruction> vector;
};

KernelPair makeAddition()
{
    return {
        "Four-element addition",
        {
            {"add", TimingOpKind::ScalarALU, "x10", {"a0", "b0"}},
            {"add", TimingOpKind::ScalarALU, "x11", {"a1", "b1"}},
            {"add", TimingOpKind::ScalarALU, "x12", {"a2", "b2"}},
            {"add", TimingOpKind::ScalarALU, "x13", {"a3", "b3"}},
        },
        {{"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 4}},
    };
}

KernelPair makeDotProduct()
{
    return {
        "Four-element dot product",
        {
            {"mul", TimingOpKind::ScalarALU, "p0", {"a0", "b0"}},
            {"mul", TimingOpKind::ScalarALU, "p1", {"a1", "b1"}},
            {"mul", TimingOpKind::ScalarALU, "p2", {"a2", "b2"}},
            {"mul", TimingOpKind::ScalarALU, "p3", {"a3", "b3"}},
            {"add", TimingOpKind::ScalarALU, "sum01", {"p0", "p1"}},
            {"add", TimingOpKind::ScalarALU, "sum012", {"sum01", "p2"}},
            {"add", TimingOpKind::ScalarALU, "dot", {"sum012", "p3"}},
        },
        {
            {"vmul.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 4},
            {"vredsum.vs", TimingOpKind::VectorReduction, "x10", {"v3", "v0"}, 4},
        },
    };
}

KernelPair makeMatrixMultiply()
{
    KernelPair pair;
    pair.name = "2x2 matrix multiplication (four dot products)";
    for (int output = 0; output < 4; ++output)
    {
        const std::string row = output < 2 ? "row0" : "row1";
        const std::string column = output % 2 == 0 ? "col0" : "col1";
        const std::string product = "p" + std::to_string(output);
        const std::string result = "c" + std::to_string(output);
        pair.scalar.push_back({"mul", TimingOpKind::ScalarALU,
                               product + "a", {row + "0", column + "0"}});
        pair.scalar.push_back({"mul", TimingOpKind::ScalarALU,
                               product + "b", {row + "1", column + "1"}});
        pair.scalar.push_back({"add", TimingOpKind::ScalarALU,
                               result, {product + "a", product + "b"}});
        pair.vector.push_back({"vmul.vv", TimingOpKind::VectorALU,
                               "vprod" + std::to_string(output),
                               {row, column}, 2});
        pair.vector.push_back({"vredsum.vs", TimingOpKind::VectorReduction,
                               result, {"vprod" + std::to_string(output), "v0"}, 2});
    }
    return pair;
}

KernelPair makeMemoryKernel(std::uint64_t extra_delay)
{
    return {
        "Vector load-add-store (memory sensitivity)",
        {},
        {
            {"vle32.v", TimingOpKind::VectorLoad, "v1", {"x5"}, 4, extra_delay},
            {"vle32.v", TimingOpKind::VectorLoad, "v2", {"x6"}, 4, extra_delay},
            {"vadd.vv", TimingOpKind::VectorALU, "v3", {"v1", "v2"}, 4},
            {"vse32.v", TimingOpKind::VectorStore, "", {"x7", "v3"}, 4, extra_delay},
        },
    };
}

void printSummary(const TimingSummary &summary, bool show_trace)
{
    std::cout << "  estimated cycles:       " << summary.estimated_cycles << '\n'
              << "  instructions:            " << summary.instruction_count << '\n'
              << "  vector instructions:     " << summary.vector_instruction_count << '\n'
              << "  active vector elements:  " << summary.active_vector_elements << '\n'
              << "  dependency stall cycles: " << summary.dependency_stall_cycles << '\n'
              << "  memory stall cycles:     " << summary.memory_stall_cycles << '\n'
              << "  lane utilisation:        " << std::fixed << std::setprecision(1)
              << (summary.lane_utilization * 100.0) << "%\n"
              << "  active elements/cycle:   " << std::setprecision(3)
              << summary.active_elements_per_cycle << '\n';

    if (!show_trace)
        return;
    std::cout << "  trace:\n"
              << "    idx  mnemonic       kind               issue  start  done  active  groups  lane-slots  dep-stall  mem-stall\n";
    for (const auto &entry : summary.trace)
    {
        std::cout << "    " << std::setw(3) << entry.instruction_index << "  "
                  << std::left << std::setw(14) << entry.mnemonic << std::setw(19)
                  << Kites::Performance::toString(entry.kind) << std::right
                  << std::setw(6) << entry.issue_cycle
                  << std::setw(7) << entry.start_cycle
                  << std::setw(6) << entry.completion_cycle
                  << std::setw(8) << entry.active_elements
                  << std::setw(8) << entry.lane_groups
                  << std::setw(11) << entry.lane_slots
                  << std::setw(11) << entry.dependency_stall_cycles
                  << std::setw(11) << entry.memory_stall_cycles << '\n';
    }
}

std::uint64_t parseUnsigned(const std::string &option, const char *name,
                             bool allow_zero)
{
    std::uint64_t value = 0;
    const char *begin = option.data();
    const char *end = begin + option.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, value, 10);
    if (error != std::errc{} || parsed_end != end || (!allow_zero && value == 0))
        throw std::invalid_argument(std::string(name) +
                                    (allow_zero ? " must be a non-negative integer"
                                                : " must be a positive integer"));
    return value;
}

std::size_t parsePositiveSize(const std::string &option, const char *name)
{
    const std::uint64_t value = parseUnsigned(option, name, false);
    if (value > std::numeric_limits<std::size_t>::max())
        throw std::out_of_range(std::string(name) + " exceeds size_t range");
    return static_cast<std::size_t>(value);
}

void printUsage()
{
    std::cout << "Usage: kites-vector-timing [options]\n"
              << "  --lanes N              vector execution lanes\n"
              << "  --issue-interval N     cycles between instruction issues\n"
              << "  --startup N            vector startup latency\n"
              << "  --vector-group N       latency per vector lane group\n"
              << "  --scalar-latency N     scalar ALU latency\n"
              << "  --reduction-extra N    additional reduction latency (zero allowed)\n"
              << "  --memory-latency N     base vector memory latency\n"
              << "  --memory-delay N       extra delay for each vector memory operation\n"
              << "  --no-trace             omit per-instruction traces\n"
              << "  --help                 show this help\n";
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        TimingConfig config;
        bool show_trace = true;
        std::uint64_t memory_delay = 0;
        for (int index = 1; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg == "--help" || arg == "-h")
            {
                printUsage();
                return 0;
            }
            else if (arg == "--no-trace")
                show_trace = false;
            else if (arg == "--lanes" && index + 1 < argc)
                config.lanes = parsePositiveSize(argv[++index], "--lanes");
            else if (arg == "--issue-interval" && index + 1 < argc)
                config.issue_interval_cycles = parseUnsigned(argv[++index], "--issue-interval", false);
            else if (arg == "--startup" && index + 1 < argc)
                config.vector_startup_latency_cycles = parseUnsigned(argv[++index], "--startup", true);
            else if (arg == "--vector-group" && index + 1 < argc)
                config.vector_group_latency_cycles = parseUnsigned(argv[++index], "--vector-group", false);
            else if (arg == "--scalar-latency" && index + 1 < argc)
                config.scalar_latency_cycles = parseUnsigned(argv[++index], "--scalar-latency", false);
            else if (arg == "--reduction-extra" && index + 1 < argc)
                config.reduction_extra_latency_cycles =
                    parseUnsigned(argv[++index], "--reduction-extra", true);
            else if (arg == "--memory-latency" && index + 1 < argc)
                config.memory_latency_cycles = parseUnsigned(argv[++index], "--memory-latency", false);
            else if (arg == "--memory-delay" && index + 1 < argc)
                memory_delay = parseUnsigned(argv[++index], "--memory-delay", true);
            else
                throw std::invalid_argument("Unknown or incomplete option: " + arg);
        }

        const VectorTimingModel model(config);
        std::cout << "Kites abstract vector timing model (not cycle-accurate hardware)\n"
                  << "Configuration: lanes=" << config.lanes
                  << ", scalar-latency=" << config.scalar_latency_cycles
                  << ", startup=" << config.vector_startup_latency_cycles
                  << ", vector-group=" << config.vector_group_latency_cycles
                  << ", reduction-extra=" << config.reduction_extra_latency_cycles
                  << ", memory-latency=" << config.memory_latency_cycles
                  << ", issue-interval=" << config.issue_interval_cycles << "\n";

        for (const auto &kernel : {makeAddition(), makeDotProduct(), makeMatrixMultiply()})
        {
            std::cout << "\n" << kernel.name << '\n';
            const auto scalar = model.simulate(kernel.scalar);
            const auto vector = model.simulate(kernel.vector);
            std::cout << " Scalar implementation:\n";
            printSummary(scalar, show_trace);
            std::cout << " Vector implementation:\n";
            printSummary(vector, show_trace);
            if (vector.estimated_cycles != 0)
                std::cout << "  estimated speedup: "
                          << std::fixed << std::setprecision(2)
                          << static_cast<double>(scalar.estimated_cycles) /
                                 static_cast<double>(vector.estimated_cycles)
                          << "x\n";
        }

        std::cout << "\nVector load-add-store (memory sensitivity)\n";
        printSummary(model.simulate(makeMemoryKernel(memory_delay).vector), show_trace);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Timing-model error: " << error.what() << '\n';
        return 2;
    }
}
