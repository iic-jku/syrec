/**
 * @file syrec_synthesis.hpp
 *
 * @brief SyReC Synthesis
 */

#ifndef SYREC_SYNTHESIS_HPP
#define SYREC_SYNTHESIS_HPP

#include <algorithm>
#include <algorithms/synthesis/synthesis.hpp>
#include <algorithms/synthesis/syrec_synthesis_p.hpp>
#include <core/circuit.hpp>
#include <core/functions/active_controls.hpp>
#include <core/functions/add_circuit.hpp>
#include <core/functor.hpp>
#include <core/properties.hpp>
#include <core/syrec/expression.hpp>
#include <core/syrec/program.hpp>
#include <core/truth_table.hpp>
#include <math.h>
#include <stack>

namespace revkit {

    namespace syrec {
        struct set_comperator {
            bool operator()(syrec::variable_access::ptr var1, syrec::variable_access::ptr var2) const {
                return var1->var().get() < var2->var().get();
            }
        };

        class program;
    } // namespace syrec
    using namespace internal;

    typedef functor<bool(circuit&, const syrec::program&)> hdl_synthesis_func;

    class standard_syrec_synthesizer {
    public:
        typedef std::map<syrec::variable::ptr, unsigned> var_lines_map;

        standard_syrec_synthesizer(circuit& circ, const syrec::program& prog);

        // zum Kompilieren unter MacOS
#ifdef __APPLE__
        virtual ~standard_syrec_synthesizer();
#else
        virtual ~standard_syrec_synthesizer();
#endif

        virtual void initialize_changing_variables(const syrec::program& program);

        virtual bool on_module(syrec::module::ptr);
        virtual bool on_statement(syrec::statement::ptr statement);
        bool         on_expression(syrec::expression::ptr expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op);
        bool         expression_op_inverse(syrec::expression::ptr expression);                           //new
        bool         var_expression(syrec::expression::ptr expression, std::vector<unsigned>& v);        //new
        bool         on_full_statement(syrec::statement::ptr statement);                                 //new
        bool         op_rhs_lhs_expression(syrec::expression::ptr expression, std::vector<unsigned>& v); //new
        bool         full_statement(syrec::statement::ptr statement);                                    //new
        bool         flow(syrec::expression::ptr expression, std::vector<unsigned>& v);                  // new
        virtual void set_settings(properties::ptr settings);
        virtual void set_main_module(syrec::module::ptr main_module);
        bool         solver(std::vector<unsigned> stat_lhs, unsigned stat_op, std::vector<unsigned> exp_lhs, unsigned exp_op, std::vector<unsigned> exp_rhs);
        bool         opt_solver(std::vector<unsigned> stat_lhs, unsigned stat_op, std::vector<unsigned> exp_lhs, unsigned exp_op, std::vector<unsigned> exp_rhs, unsigned a);
        // Virtual Methods to override for custom synthesizers
    protected:
        // statements
        virtual bool on_statement(const syrec::swap_statement& statement);
        virtual bool on_statement(const syrec::unary_statement& statement);
        virtual bool on_statement(const syrec::assign_statement& statement);
        virtual bool on_statement(const syrec::if_statement& statement);
        virtual bool on_statement(const syrec::for_statement& statement);
        virtual bool on_statement(const syrec::call_statement& statement);
        virtual bool on_statement(const syrec::uncall_statement& statement);
        virtual bool on_statement(const syrec::skip_statement& statement);
        virtual bool on_full_statement(const syrec::assign_statement& statement);                                   //new
        virtual bool var_expression(const syrec::variable_expression& expression, std::vector<unsigned>& v);        //new
        virtual bool op_rhs_lhs_expression(const syrec::variable_expression& expression, std::vector<unsigned>& v); //new
        virtual bool op_rhs_lhs_expression(const syrec::binary_expression& expression, std::vector<unsigned>& v);   //new
        virtual bool full_statement(const syrec::assign_statement& statement);                                      //new

        // expressions
        virtual bool on_expression(const syrec::numeric_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op);
        virtual bool on_expression(const syrec::variable_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op);
        virtual bool on_expression(const syrec::binary_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op);
        virtual bool on_expression(const syrec::shift_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op);

        virtual bool flow(const syrec::variable_expression& expression, std::vector<unsigned>& v); //new
        virtual bool flow(const syrec::binary_expression& expression, std::vector<unsigned>& v);   //new
                                                                                                   // Helper methods (can also be used by custom synthesizers)
    protected:
        virtual // unary operations
                bool
                     bitwise_negation(const std::vector<unsigned>& dest);                // ~ TODO: test
        virtual bool decrement(const std::vector<unsigned>& dest);                       // -- TODO: test
        bool         decrement_additionalLineMerging(const std::vector<unsigned>& dest); // -- TODO: test
        virtual bool increment(const std::vector<unsigned>& dest);                       // ++ TODO: test
        bool         increment_additionalLineMerging(const std::vector<unsigned>& dest); // ++ TODO: test

        // binary operations
        bool bitwise_and(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2); // & TODO: test
        bool bitwise_cnot(const std::vector<unsigned>& dest, const std::vector<unsigned>& src);                                    // ^=
        bool bitwise_or(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);  // & TODO: test
        bool conjunction(unsigned dest, unsigned src1, unsigned src2);                                                             // && TODO: test
        bool decrease(const std::vector<unsigned>& dest, const std::vector<unsigned>& src);                                        // -=
        bool decrease_with_carry(const std::vector<unsigned>& dest, const std::vector<unsigned>& src, unsigned carry);
        bool disjunction(unsigned dest, unsigned src1, unsigned src2);                                                          // || TODO: test
        bool division(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2); // /
        bool equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);                       // =
        bool greater_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);               // > TODO: test
        bool greater_than(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);                 // > TODO: test
        bool increase(const std::vector<unsigned>& dest, const std::vector<unsigned>& src);                                     // +=
        bool increase_with_carry(const std::vector<unsigned>& dest, const std::vector<unsigned>& src, unsigned carry);
        bool less_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);                             // <= TODO: test
        bool less_than(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);                               // < TODO: test
        bool modulo(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);              // % TODO: testen
        bool multiplication(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);      // *
        bool multiplication_full(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2); // *%* TODO: testen
        bool not_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2);                              // !=
        bool swap(const std::vector<unsigned>& dest1, const std::vector<unsigned>& dest2);                                                 // <=>
        //new
        bool findDuplicates();
        bool findInVector(std::vector<std::vector<unsigned>> vecOfElements, std::vector<unsigned> element);
        bool check_repeats();

        bool maj_2(unsigned in1, unsigned in2);
        bool maj(unsigned in1, unsigned in2, unsigned in3);
        bool uma(unsigned in1, unsigned in2, unsigned in3);
        bool uma_3cnot(unsigned in1, unsigned in2, unsigned in3);
        bool decrease_new(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs);
        bool decrease_new_assign(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs);
        bool increase_new(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs);
        bool expression_op_inverse(unsigned op, std::vector<unsigned> exp_lhs, std::vector<unsigned> exp_rhs);
        bool expression_single_op(unsigned op, std::vector<unsigned> exp_lhs, std::vector<unsigned> exp_rhs);
        bool exp_evaluate(std::vector<unsigned>& lines, unsigned op, std::vector<unsigned> lhs, std::vector<unsigned> rhs);
        //bool exp_eval( unsigned op, std::vector<unsigned> exp_lhs, std::vector<unsigned> exp_rhs, std::vector<unsigned>& lines, std::vector<unsigned> lhs_stat);
        // shift operations
        bool left_shift(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, unsigned src2);  // << TODO: testen
        bool right_shift(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, unsigned src2); // >> TODO: testen

        // efficient controls
        bool add_active_control(unsigned);
        bool remove_active_control(unsigned);

        bool assemble_circuit(const cct_node&);
        bool assemble_circuit(circuit& circ, const cct_node& current, gate::line_container controls);

        bool     optimization_decision(const cct_node&);
        unsigned bestCost(const cct_node& current);
        unsigned standardCost(const cct_node& current, unsigned controls);
        unsigned optimizationCost(const cct_node& current);
        unsigned successorsCost(const cct_node& current);

        cct_manager cct_man;

    public:
        var_lines_map& var_lines();
        //syrec::expression::ptr syrec::expression::binary_expression::lhs();

    protected:
        circuit&                           circ() const;
        std::stack<syrec::statement::ptr>& stmts();

        bool         get_variables(syrec::variable_access::ptr var, std::vector<unsigned>& lines);
        bool         unget_variables(syrec::variable_access::ptr var, std::vector<unsigned>& lines);
        bool         array_swapping(unsigned offset, std::vector<unsigned> dimensions, std::vector<std::shared_ptr<syrec::expression>> indexes, unsigned bitwidth, std::vector<unsigned>& lines);
        unsigned     get_constant_line(bool value);
        bool         get_constant_lines(unsigned bitwidth, unsigned value, std::vector<unsigned>& lines);
        virtual void release_constant_line(unsigned index, bool value);

    private:
        circuit&                                                           _circ;
        properties::ptr                                                    _settings; // Settings to use them recursively in module call
        std::stack<syrec::statement::ptr>                                  _stmts;
        var_lines_map                                                      _var_lines;
        std::map<bool, std::vector<unsigned>>                              free_const_lines_map; // TODO: set statt vector?
        syrec::number::loop_variable_mapping                               loop_map;             // TODO: umbenennen: intern_variable_mapping oder aehnlich
        std::map<syrec::variable_access::ptr, syrec::variable_access::ptr> dupl_if_var_mapping;  // duplication if: mapping of variables to there duplicates

        typedef std::set<syrec::variable_access::ptr, syrec::set_comperator> var_set;

        std::map<const syrec::statement*, var_set> _changing_variables; // for if_realization_duplication
        std::stack<syrec::module::ptr>             modules;

        void compute_changing_variables(const syrec::program& program, std::map<const syrec::statement*, var_set>& changing_variables);
        void compute_changing_variables(const syrec::module::ptr module, std::map<const syrec::statement*, var_set>& changing_variables);
        void compute_changing_variables(const syrec::statement::ptr statement, std::map<const syrec::statement*, var_set>& changing_variables);

        std::string variable_name_format;

        /**
     * @brief number of merged control lines for in-/decrement
     *
     * in the realization of the in-/decrement this number of control lines will be merged to save quantum and transistor costs; but one additional constant line is needed for that, but is released after the process.
     * if set to less than 2 the merging is deactivated
     * default value is 8.
     *
     * @author RevKit
     * @since  1.1
     */
        unsigned crement_merge_line_count;

        unsigned if_realization;

        bool efficient_controls;
    };

    /**
   * @brief IF statement realization
   *
   * @author RevKit
   * @since  1.1
   */
    enum {
        /**
     * @brief Realization by adding if condition as controlled line
     */
        syrec_synthesis_if_realization_controlled,

        /**
     * @brief Realization by using duplicated variables in else block
     */
        syrec_synthesis_if_realization_duplication
    };

    /**
   * @brief SyReC Synthesis
   *
   * TODO
   *
   * @author RevKit
   * @since  1.1
   */
    bool syrec_synthesis(circuit& circ, const syrec::program& program, properties::ptr settings = properties::ptr(), properties::ptr statistics = properties::ptr());

    /**
   * @brief Functor for the syrec_synthesis algorithm
   *
   * @param settings Settings (see syrec_synthesis)
   * @param statistics Statistics (see syrec_synthesis)
   *
   * @return A functor which complies with the hdl_synthesis_func interface
   *
   * @author RevKit
   * @since  1.1
   */
    hdl_synthesis_func syrec_synthesis_func(properties::ptr settings = properties::ptr(new properties()), properties::ptr statistics = properties::ptr(new properties()));

} // namespace revkit

#endif /* SYREC_SYNTHESIS_HPP */
