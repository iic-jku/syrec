#include "algorithms/synthesis/syrec_cost_aware_synthesis.hpp"

namespace syrec {

    void CostAware::expAdd(const unsigned& bitwidth, std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs, const std::vector<unsigned>& lines) {
        SyrecSynthesis::getConstantLines(bitwidth, 0U, rhs);
        SyrecSynthesis::bitwiseCnot(rhs, lhs); // duplicate lhs
        SyrecSynthesis::increase(rhs, lines);
    }

    void CostAware::expSubtract(const unsigned& bitwidth, std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs, const std::vector<unsigned>& lines) {
        SyrecSynthesis::getConstantLines(bitwidth, 0U, rhs);
        SyrecSynthesis::bitwiseCnot(rhs, lhs); // duplicate lhs
        SyrecSynthesis::decrease(rhs, lines);
    }

    void CostAware::expExor(const unsigned& bitwidth, std::vector<unsigned>& lines, const std::vector<unsigned>& lhs, const std::vector<unsigned>& rhs) {
        SyrecSynthesis::getConstantLines(bitwidth, 0U, lines);
        SyrecSynthesis::bitwiseCnot(lines, lhs); // duplicate lhs
        SyrecSynthesis::bitwiseCnot(lines, rhs);
    }

    bool CostAware::synthesize(Circuit& circ, const program& program, const Properties::ptr& settings, const Properties::ptr& statistics) {
        CostAware synthesizer(circ);
        return SyrecSynthesis::synthesize(&synthesizer, circ, program, settings, statistics);
    }
} // namespace syrec
