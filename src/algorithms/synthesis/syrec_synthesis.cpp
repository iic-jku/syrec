#include "algorithms/synthesis/syrec_synthesis.hpp"

#include "core/functions/add_gates.hpp"
#include "core/functions/add_line_to_circuit.hpp"
#include "core/syrec/expression.hpp"
#include "core/syrec/program.hpp"
#include "core/syrec/reverse_statements.hpp"
#include "core/syrec/variable.hpp"
#include "core/utils/timer.hpp"

#include <boost/dynamic_bitset.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptors.hpp>
#include <cmath>
#include <functional>
#include <memory>
#include <numeric>

namespace syrec {
    static std::stack<unsigned>               exp_opp;
    static std::stack<std::vector<unsigned>>  exp_lhss, exp_rhss;
    [[maybe_unused]] unsigned                 statement_op;
    bool                                      rhs_equal = false;
    bool                                      sub_flag  = false;
    static std::vector<unsigned>              op_vec, assign_op_vector, exp_op_vector;
    static std::vector<std::vector<unsigned>> lhs_vec, rhs_vec, exp_lhs_vector, exp_rhs_vector;
    static std::vector<std::vector<unsigned>> lhs_vec1, rhs_vec1;

    struct annotater {
        explicit annotater(circuit& circ, const std::stack<applications::statement::ptr>& stmts):
            _circ(circ),
            _stmts(stmts) {}

        // Operator needs this signature to work
        void operator()(gate& g) const {
            if (!_stmts.empty()) {
                _circ.annotate(g, "lno", boost::lexical_cast<std::string>(_stmts.top()->line_number()));
            }
        }

    private:
        circuit&                                        _circ;
        const std::stack<applications::statement::ptr>& _stmts;
    };

    // Helper Functions for the synthesis methods
    void add_variables(circuit& circ, standard_syrec_synthesizer& synthesizer, const std::string& variable_name_format, const applications::variable::vec& variables);
    //applications::expression::ptr applications::binary_expression::lhs() const;
    standard_syrec_synthesizer::standard_syrec_synthesizer(circuit& circ, const applications::program& prog [[maybe_unused]]):
        _circ(circ) {
        free_const_lines_map.insert(std::make_pair(false, std::vector<unsigned>()));
        free_const_lines_map.insert(std::make_pair(true, std::vector<unsigned>()));

        // root anlegen
        cct_man.current = add_vertex(cct_man.tree);
        cct_man.root    = cct_man.current;
        // get( boost::vertex_name, cct_man.tree )[cct_man.current].circ = std::shared_ptr<circuit>( new circuit() );
        // Blatt anlegen
        cct_man.current                                             = add_vertex(cct_man.tree);
        get(boost::vertex_name, cct_man.tree)[cct_man.current].circ = std::make_shared<circuit>();
        get(boost::vertex_name, cct_man.tree)[cct_man.current].circ->gate_added.connect(annotater(*get(boost::vertex_name, cct_man.tree)[cct_man.current].circ, _stmts));
        add_edge(cct_man.root, cct_man.current, cct_man.tree);
    }

    void standard_syrec_synthesizer::set_settings(const properties::ptr& settings) {
        _settings = settings;

        variable_name_format     = get<std::string>(settings, "variable_name_format", "%1$s%3$s.%2$d");
        crement_merge_line_count = get<unsigned>(settings, "crement_merge_line_count", 4u);
        if_realization           = get<unsigned>(settings, "if_realization", syrec_synthesis_if_realization_controlled);
        efficient_controls       = get<bool>(settings, "efficient_controls", false);
    }

    void standard_syrec_synthesizer::set_main_module(const applications::module::ptr& main_module) {
        assert(modules.empty());
        modules.push(main_module);
    }

    bool standard_syrec_synthesizer::on_module(const applications::module::ptr& main) {
        for (const applications::statement::ptr& stat: main->statements()) {
            if (!full_statement(stat)) {
                //  if(! on_full_statement(stat ))
                //{
                if (!on_statement(stat)) {
                    return false;
                }
                //}
            }
        }
        return assemble_circuit(cct_man.root);
    }
    /// checking the entire statement
    bool standard_syrec_synthesizer::full_statement(const applications::statement::ptr& statement) {
        bool okay = false;
        if (auto* stat = dynamic_cast<applications::assign_statement*>(statement.get())) {
            okay = full_statement(*stat);

        } else {
            return false;
        }

        return okay;
    }

    /// Expression Evaluator when the input signals are equal (the output signals are initially assigned to "0")

    bool standard_syrec_synthesizer::full_statement(const applications::assign_statement& statement) {
        std::vector<unsigned> d, dd, stat_lhs, comp, ddd;
        std::vector<unsigned> lines;
        get_variables(statement.lhs(), stat_lhs);

        //std::cout<<op_vec.size()<<std::endl; //print

        op_rhs_lhs_expression(statement.rhs(), d);

        if (op_vec.empty()) {
            return false;
        }
        flow(statement.rhs(), ddd);

        /// Only when the rhs input signals are repeated (since the results are stored in the rhs)

        if (check_repeats() and rhs_equal) {
            rhs_equal = false;

            flow(statement.rhs(), dd);

            if (exp_op_vector.size() == 1) {
                if (exp_op_vector.at(0) == 1 or exp_op_vector.at(0) == 2) {
                    /// cancel out the signals

                    exp_op_vector.clear();
                    assign_op_vector.clear();
                    exp_lhs_vector.clear();
                    exp_rhs_vector.clear();
                    op_vec.clear();
                    lhs_vec.clear();
                    rhs_vec.clear();
                    lhs_vec1.clear();
                    rhs_vec1.clear();
                } else {
                    expression_single_op(statement.op(), exp_lhs_vector.at(0), stat_lhs);
                    expression_single_op(exp_op_vector.at(0), exp_rhs_vector.at(0), stat_lhs);
                    exp_op_vector.clear();
                    assign_op_vector.clear();
                    exp_lhs_vector.clear();
                    exp_rhs_vector.clear();
                    op_vec.clear();
                    lhs_vec.clear();
                    rhs_vec.clear();
                    lhs_vec1.clear();
                    rhs_vec1.clear();
                }
            } else {
                if (exp_lhs_vector.at(0) == exp_rhs_vector.at(0)) {
                    if (exp_op_vector.at(0) == 1 or exp_op_vector.at(0) == 2) {
                        /// cancel out the signals
                    } else if (exp_op_vector.at(0) != 1 or exp_op_vector.at(0) != 2) {
                        expression_single_op(statement.op(), exp_lhs_vector.at(0), stat_lhs);
                        expression_single_op(exp_op_vector.at(0), exp_rhs_vector.at(0), stat_lhs);
                    }
                } else {
                    solver(stat_lhs, statement.op(), exp_lhs_vector.at(0), exp_op_vector.at(0), exp_rhs_vector.at(0));
                }

                unsigned              j = 0;
                unsigned              z;
                std::vector<unsigned> stat_assign_op;
                if ((exp_op_vector.size() % 2) == 0) {
                    z = ((exp_op_vector.size()) / (2));
                } else {
                    z = (((exp_op_vector.size()) - 1) / (2));
                }

                for (unsigned k = 0; k <= z - 1; k++) {
                    stat_assign_op.push_back(assign_op_vector.at(k));
                }

                /// Assignment operations

                std::reverse(stat_assign_op.begin(), stat_assign_op.end());

                /// If reversible assignment is "-", the assignment operations must negated appropriately

                if (statement.op() == 1) {
                    for (unsigned int& i: stat_assign_op) {
                        if (i == 0) {
                            i = 1;
                        } else if (i == 1) {
                            i = 0;
                        } else {
                            //stat_assign_op.at(i) == stat_assign_op.at(i);
                            continue;
                        }
                    }
                }

                for (unsigned i = 1; i <= exp_op_vector.size() - 1; i++) {
                    //unsigned j =exp_op_vector.size() -1 -i;

                    /// when both rhs and lhs exist
                    if ((exp_lhs_vector.at(i) != comp) && (exp_rhs_vector.at(i) != comp)) {
                        if (exp_lhs_vector.at(i) == exp_rhs_vector.at(i)) {
                            if (exp_op_vector.at(i) == 1 or exp_op_vector.at(i) == 2) {
                                /// cancel out the signals
                                j = j + 1;
                            } else if (exp_op_vector.at(i) != 1 or exp_op_vector.at(i) != 2) {
                                expression_single_op(stat_assign_op.at(j), exp_lhs_vector.at(i), stat_lhs);
                                expression_single_op(exp_op_vector.at(i), exp_rhs_vector.at(i), stat_lhs);
                                j = j + 1;
                            }
                        } else {
                            solver(stat_lhs, stat_assign_op.at(j), exp_lhs_vector.at(i), exp_op_vector.at(i), exp_rhs_vector.at(i));
                            j = j + 1;
                        }
                    }

                    /// when only rhs exists
                    else if ((exp_lhs_vector.at(i) == comp) && (exp_rhs_vector.at(i) != comp)) {
                        exp_evaluate(lines, stat_assign_op.at(j), exp_rhs_vector.at(i), stat_lhs);
                        //expression_single_op(stat_assign_op.at(j),exp_rhs_vector.at(i),stat_lhs);
                        j = j + 1;
                    }

                    /// when only lhs exists
                    else if ((exp_lhs_vector.at(i) != comp) && (exp_rhs_vector.at(i) == comp)) {
                        exp_evaluate(lines, stat_assign_op.at(j), exp_rhs_vector.at(i), stat_lhs);
                        //expression_single_op(stat_assign_op.at(j),exp_lhs_vector.at(i),stat_lhs);
                        j = j + 1;
                    } else if ((exp_lhs_vector.at(i) == comp) && (exp_rhs_vector.at(i) == comp)) {
                    }
                }
                exp_op_vector.clear();
                assign_op_vector.clear();
                exp_lhs_vector.clear();
                exp_rhs_vector.clear();
                op_vec.clear();
                lhs_vec.clear();
                rhs_vec.clear();
                lhs_vec1.clear();
                rhs_vec1.clear();
            }
        } else {
            exp_op_vector.clear();
            assign_op_vector.clear();
            exp_lhs_vector.clear();
            exp_rhs_vector.clear();
            op_vec.clear();
            lhs_vec.clear();
            rhs_vec.clear();
            lhs_vec1.clear();
            rhs_vec1.clear();
            return false;
        }

        exp_op_vector.clear();
        assign_op_vector.clear();
        exp_lhs_vector.clear();
        exp_rhs_vector.clear();
        op_vec.clear();
        lhs_vec.clear();
        rhs_vec.clear();
        lhs_vec1.clear();
        rhs_vec1.clear();
        return true;
    }

    bool standard_syrec_synthesizer::flow(const applications::expression::ptr& expression, std::vector<unsigned>& v) {
        if (auto* exp = dynamic_cast<applications::binary_expression*>(expression.get())) {
            return flow(*exp, v);
        } else if (auto* exp_1 = dynamic_cast<applications::variable_expression*>(expression.get())) {
            return flow(*exp_1, v);
        } else {
            return false;
        }
    }

    bool standard_syrec_synthesizer::flow(const applications::variable_expression& expression, std::vector<unsigned>& v) {
        return get_variables(expression.var(), v);
    }

    /// generating LHS and RHS (can be whole expressions as well)
    bool standard_syrec_synthesizer::flow(const applications::binary_expression& expression, std::vector<unsigned>& v [[maybe_unused]]) {
        std::vector<unsigned> lhs, rhs, comp;
        assign_op_vector.push_back(expression.op());

        if (!flow(expression.lhs(), lhs) || !flow(expression.rhs(), rhs)) {
            return false;
        }
        //v = rhs;

        exp_lhs_vector.push_back(lhs);
        exp_rhs_vector.push_back(rhs);
        exp_op_vector.push_back(expression.op());
        return true;
    }

    bool standard_syrec_synthesizer::solver(const std::vector<unsigned>& stat_lhs, unsigned stat_op, const std::vector<unsigned>& exp_lhs, unsigned exp_op, const std::vector<unsigned>& exp_rhs) {
        std::vector<unsigned> lines;
        if (stat_op == exp_op) {
            if (exp_op == 1) {
                expression_single_op(1, exp_lhs, stat_lhs);
                expression_single_op(0, exp_rhs, stat_lhs);
            } else {
                expression_single_op(stat_op, exp_lhs, stat_lhs);
                expression_single_op(stat_op, exp_rhs, stat_lhs);
            }
        } else { //expression_single_op(exp_op, exp_lhs , exp_rhs);
            //expression_single_op(stat_op, exp_rhs , stat_lhs);
            sub_flag = true;
            exp_evaluate(lines, exp_op, exp_lhs, exp_rhs);
            sub_flag = false;
            exp_evaluate(lines, stat_op, lines, stat_lhs);
            sub_flag = true;
            if (exp_op < 3) {
                expression_op_inverse(exp_op, exp_lhs, exp_rhs);
            }
        }
        sub_flag = false;
        return true;
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::on_full_statement(const applications::statement::ptr& statement) {
        bool okay = false;
        if (auto* stat = dynamic_cast<applications::assign_statement*>(statement.get())) {
            okay = on_full_statement(*stat);

        } else {
            return false;
        }

        return okay;
    }

    /// If the input signals are repeated (i.e., rhs input signals are repeated)
    bool standard_syrec_synthesizer::check_repeats() {
        std::vector<std::vector<unsigned>> check_lhs_vec, check_rhs_vec;
        std::vector<unsigned>              comp_repeats;

        for (const auto& k: exp_lhs_vector) {
            check_lhs_vec.push_back(k);
        }

        for (const auto& k: exp_rhs_vector) {
            check_rhs_vec.push_back(k);
        }

        for (unsigned k = 0; k < check_lhs_vec.size(); k++) {
            if (check_lhs_vec.at(k) == comp_repeats) {
                check_lhs_vec.erase(check_lhs_vec.begin() + (k));
            }
        }

        for (unsigned k = 0; k < check_rhs_vec.size(); k++) {
            if (check_rhs_vec.at(k) == comp_repeats) {
                check_rhs_vec.erase(check_rhs_vec.begin() + (k));
            }
        }

        /*	for (int i = 0;i < check_lhs_vec.size(); i++) {
        for (int j = 0; j < check_lhs_vec.size(); j++){
            if(j != i) { 
                if(check_lhs_vec.at(i) == check_lhs_vec.at(j)) {
		exp_op_vector.clear(); exp_lhs_vector.clear(); exp_rhs_vector.clear();
		lhs_equal=true;
		return true;}
		}}}*/

        for (int i = 0; i < int(check_rhs_vec.size()); i++) {
            for (int j = 0; j < int(check_rhs_vec.size()); j++) {
                if (j != i) {
                    if (check_rhs_vec.at(i) == check_rhs_vec.at(j)) {
                        exp_op_vector.clear();
                        exp_lhs_vector.clear();
                        exp_rhs_vector.clear();
                        rhs_equal = true;
                        return true;
                    }
                }
            }
        }

        for (auto& i: check_lhs_vec) {
            for (auto& j: check_rhs_vec) {
                if (i == j) {
                    exp_op_vector.clear();
                    exp_lhs_vector.clear();
                    exp_rhs_vector.clear();
                    rhs_equal = true;
                    return true;
                }
            }
        }

        exp_op_vector.clear();
        exp_lhs_vector.clear();
        exp_rhs_vector.clear();
        return false;
    }

    /// Currently not used (when all operations are same)
    bool standard_syrec_synthesizer::on_full_statement(const applications::assign_statement& statement) {
        bool                  ok = false;
        std::vector<unsigned> d, stat_lhs;
        unsigned              i;
        get_variables(statement.lhs(), stat_lhs);
        op_rhs_lhs_expression(statement.rhs(), d);
        if (op_vec.empty()) {
            return false;
        }
        unsigned op_size = op_vec.size();

        ok = check_repeats();

        if (op_size == 1) {
            if (statement.op() == op_vec[op_size - 1]) {
                if (op_vec[op_size - 1] == 1) {
                    expression_single_op(1, lhs_vec[op_size - 1], stat_lhs);
                    expression_single_op(0, lhs_vec[op_size - 1], stat_lhs);
                } else {
                    expression_single_op(op_vec[op_size - 1], lhs_vec[op_size - 1], stat_lhs);
                    expression_single_op(op_vec[op_size - 1], rhs_vec[op_size - 1], stat_lhs);
                }

            } else if (statement.op() != op_vec[op_size - 1]) {
                op_vec.clear();
                lhs_vec.clear();
                rhs_vec.clear();
                lhs_vec1.clear();
                rhs_vec1.clear();
                return false;
            }
        }

        else if ((statement.op() == op_vec[op_size - 1]) && (!ok)) {
            for (i = 0; i < op_size - 1; ++i) {
                expression_single_op(op_vec[i], lhs_vec[i], rhs_vec[i]);
            }

            if (op_vec[op_size - 1] == 1) {
                expression_single_op(1, lhs_vec[op_size - 1], stat_lhs);
                expression_single_op(0, lhs_vec[op_size - 1], stat_lhs);
            } else {
                expression_single_op(op_vec[op_size - 1], lhs_vec[op_size - 1], stat_lhs);
                expression_single_op(op_vec[op_size - 1], rhs_vec[op_size - 1], stat_lhs);
            }

            lhs_vec.pop_back();
            rhs_vec.pop_back();
            op_vec.pop_back();
            while (!op_vec.empty()) {
                expression_op_inverse(op_vec.back(), lhs_vec.back(), rhs_vec.back());
                lhs_vec.pop_back();
                rhs_vec.pop_back();
                op_vec.pop_back();
            }

        }

        else {
            op_vec.clear();
            lhs_vec.clear();
            rhs_vec.clear();
            lhs_vec1.clear();
            rhs_vec1.clear();
            return false;
        }
        op_vec.clear();
        lhs_vec.clear();
        rhs_vec.clear();
        // op_vec.clear();
        lhs_vec1.clear();
        rhs_vec1.clear();

        return true;
    }

    /// generating LHS and RHS (not whole expressions, just the corresponding variables)
    bool standard_syrec_synthesizer::op_rhs_lhs_expression(const applications::expression::ptr& expression, std::vector<unsigned>& v) {
        if (auto* exp = dynamic_cast<applications::binary_expression*>(expression.get())) {
            return op_rhs_lhs_expression(*exp, v);
        } else if (auto* exp_2 = dynamic_cast<applications::variable_expression*>(expression.get())) {
            return op_rhs_lhs_expression(*exp_2, v);
        } else {
            return false;
        }
    }

    bool standard_syrec_synthesizer::op_rhs_lhs_expression(const applications::variable_expression& expression, std::vector<unsigned>& v) {
        return get_variables(expression.var(), v);
    }

    bool standard_syrec_synthesizer::op_rhs_lhs_expression(const applications::binary_expression& expression, std::vector<unsigned>& v) {
        std::vector<unsigned> lhs, rhs;

        if (!op_rhs_lhs_expression(expression.lhs(), lhs) || !op_rhs_lhs_expression(expression.rhs(), rhs)) {
            return false;
        }

        v = rhs;
        lhs_vec.push_back(lhs);
        lhs_vec1.push_back(lhs);
        rhs_vec.push_back(rhs);
        rhs_vec1.push_back(rhs);
        op_vec.push_back(expression.op());
        return true;
    }

    /////////When the input signals are not repeated//////////////////
    bool standard_syrec_synthesizer::on_statement(const applications::statement::ptr& statement) {
        _stmts.push(statement);
        bool okay = false;
        if (auto* stat = dynamic_cast<applications::swap_statement*>(statement.get())) {
            okay = on_statement(*stat);
        } else if (auto* stat_1 = dynamic_cast<applications::unary_statement*>(statement.get())) {
            okay = on_statement(*stat_1);
        } else if (auto* stat_2 = dynamic_cast<applications::assign_statement*>(statement.get())) {
            okay = on_statement(*stat_2);
        } else if (auto* stat_3 = dynamic_cast<applications::if_statement*>(statement.get())) {
            okay = on_statement(*stat_3);
        } else if (auto* stat_4 = dynamic_cast<applications::for_statement*>(statement.get())) {
            okay = on_statement(*stat_4);
        } else if (auto* stat_5 = dynamic_cast<applications::call_statement*>(statement.get())) {
            okay = on_statement(*stat_5);
        } else if (auto* stat_6 = dynamic_cast<applications::uncall_statement*>(statement.get())) {
            okay = on_statement(*stat_6);
        } else if (auto* stat_7 = dynamic_cast<applications::skip_statement*>(statement.get())) {
            okay = on_statement(*stat_7);
        } else {
            return false;
        }

        _stmts.pop();
        return okay;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::swap_statement& statement) {
        std::vector<unsigned> lhs, rhs;

        get_variables(statement.lhs(), lhs);
        get_variables(statement.rhs(), rhs);

        assert(lhs.size() == rhs.size());

        swap(lhs, rhs);

        unget_variables(statement.lhs(), lhs);
        unget_variables(statement.rhs(), rhs);

        return true;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::unary_statement& statement) {
        // load variable
        std::vector<unsigned> var;
        get_variables(statement.var(), var);

        switch (statement.op()) {
            case applications::unary_statement::invert: {
                bitwise_negation(var);
            } break;

            case applications::unary_statement::increment: {
                /*if ( crement_merge_line_count > 1u && crement_merge_line_count < var.size() )
      {
        // Optimized Version via additional lines optimization
        increment_additionalLineMerging( var );
      }
      else
      {*/
                // Default Version
                increment(var);
                //}
            } break;

            case applications::unary_statement::decrement: {
                /*if ( ( crement_merge_line_count > 1u ) && ( crement_merge_line_count < var.size() ) )
      {
        // Optimized version via additional lines optimization
        decrement_additionalLineMerging( var );
      }
      else
      {*/
                // Default version
                decrement(var);
                //}
            } break;
            default: {
                return false;
            }
        }
        unget_variables(statement.var(), var);

        return true;
    }

    /// Function when the assignment statements does not include repeated input signals
    bool standard_syrec_synthesizer::on_statement(const applications::assign_statement& statement) {
        std::stack<std::vector<unsigned>> exp_lhs, exp_rhs;
        std::stack<unsigned>              exp_op;
        std::vector<unsigned>             lhs, rhs, d;

        statement_op = statement.op();

        get_variables(statement.lhs(), lhs);

        op_rhs_lhs_expression(statement.rhs(), d);
        on_expression(statement.rhs(), rhs, lhs, statement.op());
        op_vec.clear();
        lhs_vec.clear();
        rhs_vec.clear();
        // op_vec.clear();
        lhs_vec1.clear();
        rhs_vec1.clear();
        // (experssion,lines,lhs_statement)
        // statement_lhs = lhs;
        // assert( lhs.size() == rhs.size() ); //stanmay

        bool status = false;

        switch (statement.op()) {
            case applications::assign_statement::add: {
                if (exp_opp.empty()) {
                    status = increase_new(lhs, rhs);
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                } else if (exp_opp.top() == statement.op()) {
                    status = increase_new(lhs, exp_lhss.top());
                    status = increase_new(lhs, exp_rhss.top());
                    exp_opp.pop();
                    exp_lhss.pop();
                    exp_rhss.pop();
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                } else {
                    status = increase_new(lhs, rhs);
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                }
            } break;

            case applications::assign_statement::subtract: {
                if (exp_opp.empty()) {
                    status = decrease_new(lhs, rhs);
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                } else if (exp_opp.top() == statement.op()) {
                    status = decrease_new(lhs, exp_lhss.top());
                    status = increase_new(lhs, exp_rhss.top());
                    exp_opp.pop();
                    exp_lhss.pop();
                    exp_rhss.pop();
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                } else {
                    status = decrease_new(lhs, rhs);
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                }
            } break;

            case applications::assign_statement::exor: {
                if (exp_opp.empty()) {
                    status = bitwise_cnot(lhs, rhs);
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                } else if (exp_opp.top() == statement.op()) {
                    status = bitwise_cnot(lhs, exp_lhss.top());
                    status = bitwise_cnot(lhs, exp_rhss.top());
                    exp_opp.pop();
                    exp_lhss.pop();
                    exp_rhss.pop();
                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                }

                else {
                    status = bitwise_cnot(lhs, rhs);

                    while (!exp_opp.empty()) {
                        expression_op_inverse(exp_opp.top(), exp_lhss.top(), exp_rhss.top());
                        sub_flag = false;
                        exp_opp.pop();
                        exp_lhss.pop();
                        exp_rhss.pop();
                    }
                    // expression_op_inverse(statement.rhs());
                }
            } break;

            default:

                return false;
        }

        //empty the stack

        unget_variables(statement.lhs(), lhs);
        return status;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::if_statement& statement) {
        // calculate expression
        std::vector<unsigned>             expression_result, lhs_stat;
        std::stack<unsigned>              dummy;
        unsigned                          op = 0u;
        std::stack<std::vector<unsigned>> exp_lhs1, exp_rhs1;
        on_expression(statement.condition(), expression_result, lhs_stat, op);
        assert(expression_result.size() == 1u);

        // add new helper line
        unsigned helper_line = expression_result.front();

        switch (if_realization) {
            case syrec_synthesis_if_realization_controlled: {
                // activate this line
                add_active_control(helper_line);

                for (const applications::statement::ptr& stat: statement.then_statements()) {
                    if (!full_statement(stat)) {
                        //  if(! on_full_statement(stat ))
                        //{
                        if (!on_statement(stat)) {
                            return false;
                        }
                        //}
                    }
                }

                // toggle helper line
                remove_active_control(helper_line);
                append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helper_line);
                add_active_control(helper_line);

                for (const applications::statement::ptr& stat: statement.else_statements()) {
                    if (!full_statement(stat)) {
                        //if(! on_full_statement(stat ))
                        //{
                        if (!on_statement(stat)) {
                            return false;
                        }
                        //}
                    }
                }

                // de-active helper line
                remove_active_control(helper_line);
                append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helper_line);

            } break;
            case syrec_synthesis_if_realization_duplication: {
                std::map<applications::variable_access::ptr, applications::variable_access::ptr> then_var_mapping = dupl_if_var_mapping;
                for (const applications::variable_access::ptr& var: _changing_variables.find(&statement)->second) {
                    applications::variable_access::ptr dupl_var = std::make_shared<applications::variable_access>(std::make_shared<applications::variable>(applications::variable::wire, ("dupl_" + var->var()->name()), var->var()->dimensions(), var->var()->bitwidth()));

                    applications::variable::vec var_vec;
                    var_vec.push_back(dupl_var->var());

                    add_variables(_circ, *this, variable_name_format, var_vec);

                    std::vector<unsigned> lhs, rhs;
                    get_variables(dupl_var, lhs);
                    get_variables(var, rhs);

                    bitwise_cnot(lhs, rhs);

                    then_var_mapping.insert(std::make_pair(var, dupl_var));
                }

                dupl_if_var_mapping.swap(then_var_mapping);

                // then branch
                for (const applications::statement::ptr& stat: statement.then_statements()) {
                    if (!full_statement(stat)) {
                        // if(! on_full_statement(stat ))
                        //{
                        if (!on_statement(stat)) {
                            return false;
                        }
                        //}
                    }
                }

                dupl_if_var_mapping.swap(then_var_mapping);

                // else branch
                for (const applications::statement::ptr& stat: statement.else_statements()) {
                    if (!full_statement(stat)) {
                        //if(! on_full_statement(stat ))
                        //{
                        if (!on_statement(stat)) {
                            return false;
                        }
                        //}
                    }
                }

                add_active_control(helper_line);
                for (const applications::variable_access::ptr& var: _changing_variables.find(&statement)->second) {
                    std::vector<unsigned> lhs, rhs;
                    get_variables(then_var_mapping.find(var)->second, lhs);
                    get_variables(var, rhs);

                    swap(lhs, rhs);
                }
                remove_active_control(helper_line);
            } break;
            default: {
            }
        }

        return true;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::for_statement& statement) {
        auto [nfrom, nto] = statement.range();

        unsigned from = nfrom ? nfrom->evaluate(loop_map) : 1u; // default value is 1u
        unsigned to   = nto->evaluate(loop_map);

        assert(to >= from);
        unsigned step = statement.step() ? statement.step()->evaluate(loop_map) : 1u; // default step is +1

        const std::string& loop_variable = statement.loop_variable();

        for (unsigned i = from; i <= to; i += step) {
            // adjust loop variable if necessary
            if (!loop_variable.empty()) {
                loop_map[loop_variable] = i;
            }

            for (const applications::statement::ptr& stat: statement.statements()) {
                if (!full_statement(stat)) {
                    // if(! on_full_statement(stat ))
                    //{
                    if (!on_statement(stat)) {
                        return false;
                    }
                    //}
                }
            }
        }

        // clear loop variable if necessary
        if (!loop_variable.empty()) {
            assert(loop_map.erase(loop_variable) == 1u);
        }

        return true;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::call_statement& statement) {
        if (_settings->get<bool>("modules_hierarchy", false)) {
            // Alternative implementation
            const std::string& module_name = statement.target()->name();
            if (_circ.modules().find(module_name) == _circ.modules().end()) {
                circuit               c_module;
                applications::program prog_module;
                prog_module.add_module(statement.target());

                syrec_synthesis(c_module, prog_module, _settings);

                _circ.add_module(module_name, c_module);
                get(boost::vertex_name, cct_man.tree)[cct_man.current].circ->add_module(module_name, c_module);
            }

            // create targets, first parameters
            std::vector<unsigned> targets;
            for (const std::string& parameter: statement.parameters()) {
                applications::variable::ptr var = modules.top()->find_parameter_or_variable(parameter);
                for (std::size_t v = 0U; v < var->bitwidth(); ++v) {
                    targets.emplace_back(_var_lines[var] + v);
                }
            }

            // now constant lines (they are always in order)
            const circuit& module = *(_circ.modules().find(module_name)->second);
            for (const constant& c: module.constants()) {
                if (c) {
                    targets.emplace_back(get_constant_line(*c));
                }
            }

            append_module(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), module_name, gate::line_container(), targets);

            // reuse lines
            unsigned i = 0u;
            for (const constant& c: module.constants()) {
                if (c) {
                    if (module.outputs().at(i) == "const_0") {
                        free_const_lines_map[false].emplace_back(targets.at(i));
                    } else if (module.outputs().at(i) == "const_1") {
                        free_const_lines_map[true].emplace_back(targets.at(i));
                    }
                }

                ++i;
            }
        } else {
            // 1. Adjust the references module's parameters to the call arguments
            for (unsigned i = 0u; i < statement.parameters().size(); ++i) {
                const std::string&                 parameter        = statement.parameters().at(i);
                const applications::variable::ptr& module_parameter = statement.target()->parameters().at(i);

                module_parameter->set_reference(modules.top()->find_parameter_or_variable(parameter));
            }

            // 2. Create new lines for the module's variables
            add_variables(_circ, *this, variable_name_format, statement.target()->variables());

            modules.push(statement.target());
            for (const applications::statement::ptr& stat: statement.target()->statements()) {
                if (!full_statement(stat)) {
                    // if(! on_full_statement(stat ))
                    //{
                    if (!on_statement(stat)) {
                        return false;
                    }
                    //}
                }
            }

            modules.pop();
        }
        return true;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::uncall_statement& statement) {
        using boost::adaptors::reversed;
        using boost::adaptors::transformed;

        // 1. Adjust the references module's parameters to the call arguments
        for (unsigned i = 0u; i < statement.parameters().size(); ++i) {
            const std::string&                 parameter        = statement.parameters().at(i);
            const applications::variable::ptr& module_parameter = statement.target()->parameters().at(i);

            module_parameter->set_reference(modules.top()->find_parameter_or_variable(parameter));
        }

        // 2. Create new lines for the module's variables
        add_variables(_circ, *this, variable_name_format, statement.target()->variables());

        modules.push(statement.target());
        for (applications::statement::ptr stat: statement.target()->statements() | reversed | transformed(applications::reverse_statements())) {
            if (!full_statement(stat)) {
                //  if(! on_full_statement(stat ))
                //{
                if (!on_statement(stat)) {
                    return false;
                }
                //}
            }
        }

        modules.pop();

        return true;
    }

    bool standard_syrec_synthesizer::on_statement(const applications::skip_statement& statement [[maybe_unused]]) {
        return true;
    }

    bool standard_syrec_synthesizer::on_expression(const applications::expression::ptr& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op) {
        if (auto* exp = dynamic_cast<applications::numeric_expression*>(expression.get())) {
            return on_expression(*exp, lines, lhs_stat, op);
        } else if (auto* exp_3 = dynamic_cast<applications::variable_expression*>(expression.get())) {
            return on_expression(*exp_3, lines, lhs_stat, op);
        } else if (auto* exp_4 = dynamic_cast<applications::binary_expression*>(expression.get())) {
            return on_expression(*exp_4, lines, lhs_stat, op);
        } else if (auto* exp_5 = dynamic_cast<applications::shift_expression*>(expression.get())) {
            return on_expression(*exp_5, lines, lhs_stat, op);
        } else {
            return false;
        }
    }

    bool standard_syrec_synthesizer::on_expression(const applications::numeric_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat [[maybe_unused]], unsigned op [[maybe_unused]]) {
        return get_constant_lines(expression.bitwidth(), expression.value()->evaluate(loop_map), lines);
    }

    bool standard_syrec_synthesizer::on_expression(const applications::variable_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat [[maybe_unused]], unsigned op [[maybe_unused]]) {
        return get_variables(expression.var(), lines);
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::var_expression(const applications::expression::ptr& expression, std::vector<unsigned>& v) {
        if (auto* exp = dynamic_cast<applications::variable_expression*>(expression.get())) {
            return var_expression(*exp, v);
        } else {
            return false;
        }
    }
    bool standard_syrec_synthesizer::var_expression(const applications::variable_expression& expression, std::vector<unsigned>& v) {
        return get_variables(expression.var(), v);
    }

    /// Function when the assignment statements consist of binary expressions and does not include repeated input signals

    bool standard_syrec_synthesizer::on_expression(const applications::binary_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op) {
        std::vector<unsigned>             lhs, rhs;
        std::stack<unsigned>              exp_op1, exp_op2;
        std::stack<std::vector<unsigned>> exp_lhs1, exp_rhs1, exp_lhs2, exp_rhs2;
        // static int ii = 0;
        unsigned size = op_vec.size();

        if (!on_expression(expression.lhs(), lhs, lhs_stat, op) || !on_expression(expression.rhs(), rhs, lhs_stat, op)) {
            return false;
        }

        exp_lhss.push(lhs);
        exp_rhss.push(rhs);
        exp_opp.push(expression.op());

        if (exp_opp.size() == size) {
            if (exp_opp.top() == op) {
                return true;
            }
        }

        switch (expression.op()) {
            case applications::binary_expression::add: // +tanmay
            {
                increase_new(rhs, lhs); // edited_by_st (lines,rhs)
                lines = rhs;

            } break;

            case applications::binary_expression::subtract: // -
            {
                // get_constant_lines( expression.bitwidth(), 0u, lines );-[
                //bitwise_cnot( lines, lhs ); // duplicate lhs

                decrease_new_assign(rhs, lhs);
                lines = rhs;
            } break;

            case applications::binary_expression::exor: // ^
            {
                //get_constant_lines( expression.bitwidth(), 0u, lines );

                bitwise_cnot(rhs, lhs); // duplicate lhs
                lines = rhs;
                // bitwise_cnot( lines, rhs );
            } break;

            case applications::binary_expression::multiply: // *
            {
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), lines);

                multiplication(lines, lhs, rhs);

            } break;

            case applications::binary_expression::divide: // /
            {
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), lines);

                division(lines, lhs, rhs);
            } break;

            case applications::binary_expression::modulo: {
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), lines);
                std::vector<unsigned> quot;
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), quot);

                bitwise_cnot(lines, lhs); // duplicate lhs
                modulo(quot, lines, rhs);
            } break;

            case applications::binary_expression::frac_divide: {
                std::vector<unsigned> product;
                get_constant_lines((expression.bitwidth()), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), product);

                copy(lines.begin(), lines.end(), back_inserter(product));

                multiplication_full(product, lhs, rhs);
            } break;

            case applications::binary_expression::logical_and: // &&
            {
                lines.emplace_back(get_constant_line(true));

                conjunction(lines.at(0), lhs.at(0), rhs.at(0));
            } break;

            case applications::binary_expression::logical_or: // ||
            {
                lines.emplace_back(get_constant_line(true));

                disjunction(lines.at(0), lhs.at(0), rhs.at(0));
            } break;

            case applications::binary_expression::bitwise_and: // &
            {
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), lines);

                bitwise_and(lines, lhs, rhs);
            } break;

            case applications::binary_expression::bitwise_or: // |
            {
                get_constant_lines(expression.bitwidth(), (unsigned)(((int)pow(2, (int)expression.bitwidth())) - 1), lines);

                bitwise_or(lines, lhs, rhs);
            } break;

            case applications::binary_expression::less_than: // <
            {
                lines.emplace_back(get_constant_line(true));

                less_than(lines.at(0), lhs, rhs);
            } break;

            case applications::binary_expression::greater_than: // >
            {
                lines.emplace_back(get_constant_line(true));

                greater_than(lines.at(0), lhs, rhs);
            } break;

            case applications::binary_expression::equals: // =
            {
                lines.emplace_back(get_constant_line(true));

                equals(lines.at(0), lhs, rhs);
            } break;

            case applications::binary_expression::not_equals: // !=
            {
                lines.emplace_back(get_constant_line(true));

                not_equals(lines.at(0), lhs, rhs);
            } break;

            case applications::binary_expression::less_equals: // <=
            {
                lines.emplace_back(get_constant_line(true));

                less_equals(lines.at(0), lhs, rhs);
            } break;

            case applications::binary_expression::greater_equals: // >=
            {
                lines.emplace_back(get_constant_line(true));

                greater_equals(lines.at(0), lhs, rhs);
            } break;

            default:

                return false;
        }

        return true;
    }

    /// This function is used when input signals (rhs) are equal (just to solve statements individually)
    bool standard_syrec_synthesizer::exp_evaluate(std::vector<unsigned>& lines, unsigned op, const std::vector<unsigned>& lhs, const std::vector<unsigned>& rhs) {
        switch (op) {
            case applications::binary_expression::add: // +tanmay
            {
                increase_new(rhs, lhs); // edited_by_st (lines,rhs)
                lines = rhs;

            } break;

            case applications::binary_expression::subtract: // -
            {
                // get_constant_lines( expression.bitwidth(), 0u, lines );-[
                //bitwise_cnot( lines, lhs ); // duplicate lhs
                if (sub_flag) {
                    decrease_new_assign(rhs, lhs);
                    lines = rhs;
                } else {
                    decrease_new(rhs, lhs);
                    lines = rhs;
                }

            } break;

            case applications::binary_expression::exor: // ^
            {
                //get_constant_lines( expression.bitwidth(), 0u, lines );

                bitwise_cnot(rhs, lhs); // duplicate lhs
                lines = rhs;
                // bitwise_cnot( lines, rhs );
            } break;

            case applications::binary_expression::multiply: // *
            {
                std::vector<unsigned> extra_lines_1;
                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), extra_lines_1);

                multiplication(extra_lines_1, lhs, rhs);
                lines = extra_lines_1;
            } break;

            case applications::binary_expression::divide: // /
            {
                std::vector<unsigned> extra_lines_2;
                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), extra_lines_2);

                division(extra_lines_2, lhs, rhs);
                lines = extra_lines_2;
            } break;

            case applications::binary_expression::modulo: {
                std::vector<unsigned> extra_lines_3;
                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), extra_lines_3);
                std::vector<unsigned> quot;
                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), quot);

                bitwise_cnot(extra_lines_3, lhs); // duplicate lhs
                modulo(quot, extra_lines_3, rhs);
                lines = extra_lines_3;
            } break;

            case applications::binary_expression::frac_divide: {
                std::vector<unsigned> product;
                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), product);

                copy(lines.begin(), lines.end(), back_inserter(product));

                multiplication_full(product, lhs, rhs);
            } break;

            case applications::binary_expression::logical_and: // &&
            {
                std::vector<unsigned> extra_lines_5;
                extra_lines_5.emplace_back(get_constant_line(true));

                conjunction(extra_lines_5.at(0), extra_lines_5.at(0), extra_lines_5.at(0));
                lines = extra_lines_5;
            } break;

            case applications::binary_expression::logical_or: // ||
            {
                std::vector<unsigned> extra_lines_6;
                extra_lines_6.emplace_back(get_constant_line(true));

                disjunction(extra_lines_6.at(0), extra_lines_6.at(0), extra_lines_6.at(0));
                lines = extra_lines_6;
            } break;

            case applications::binary_expression::bitwise_and: // &
            {
                std::vector<unsigned> extra_lines_7;

                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), extra_lines_7);

                bitwise_and(extra_lines_7, lhs, rhs);
                lines = extra_lines_7;
            } break;

            case applications::binary_expression::bitwise_or: // |
            {
                std::vector<unsigned> extra_lines_8;

                get_constant_lines(rhs.size(), (unsigned)(((int)pow(2, (int)rhs.size())) - 1), extra_lines_8);

                bitwise_or(extra_lines_8, lhs, rhs);
                lines = extra_lines_8;
            } break;

            case applications::binary_expression::less_than: // <
            {
                std::vector<unsigned> extra_lines_9;
                extra_lines_9.emplace_back(get_constant_line(true));

                less_than(extra_lines_9.at(0), lhs, rhs);
                lines = extra_lines_9;
            } break;

            case applications::binary_expression::greater_than: // >
            {
                std::vector<unsigned> extra_lines_10;
                extra_lines_10.emplace_back(get_constant_line(true));

                greater_than(extra_lines_10.at(0), lhs, rhs);
                lines = extra_lines_10;
            } break;

            case applications::binary_expression::equals: // =
            {
                std::vector<unsigned> extra_lines_11;
                extra_lines_11.emplace_back(get_constant_line(true));

                equals(extra_lines_11.at(0), lhs, rhs);
                lines = extra_lines_11;
            } break;

            case applications::binary_expression::not_equals: // !=
            {
                std::vector<unsigned> extra_lines_12;
                extra_lines_12.emplace_back(get_constant_line(true));

                not_equals(extra_lines_12.at(0), lhs, rhs);
                lines = extra_lines_12;
            } break;

            case applications::binary_expression::less_equals: // <=
            {
                std::vector<unsigned> extra_lines_13;
                extra_lines_13.emplace_back(get_constant_line(true));

                less_equals(extra_lines_13.at(0), lhs, rhs);
                lines = extra_lines_13;
            } break;

            case applications::binary_expression::greater_equals: // >=
            {
                std::vector<unsigned> extra_lines_14;
                extra_lines_14.emplace_back(get_constant_line(true));

                greater_equals(extra_lines_14.at(0), lhs, rhs);
                lines = extra_lines_14;
            } break;

            default:

                return false;
        }

        return true;
    }

    bool standard_syrec_synthesizer::on_expression(const applications::shift_expression& expression, std::vector<unsigned>& lines, std::vector<unsigned>& lhs_stat, unsigned op) {
        std::vector<unsigned>             lhs;
        std::stack<unsigned>              exp_op1;
        std::stack<std::vector<unsigned>> exp_lhs1, exp_rhs1;
        if (!on_expression(expression.lhs(), lhs, lhs_stat, op)) {
            return false;
        }

        unsigned rhs = expression.rhs()->evaluate(loop_map);

        switch (expression.op()) {
            case applications::shift_expression::left: // <<
            {
                get_constant_lines(expression.bitwidth(), 0u, lines);

                left_shift(lines, lhs, rhs);
            } break;

            case applications::shift_expression::right: // <<
            {
                get_constant_lines(expression.bitwidth(), 0u, lines);

                right_shift(lines, lhs, rhs);
            } break;

            default:

                return false;
        }

        return true;
    }

    //**********************************************************************
    //*****                      Unary Operations                      *****
    //**********************************************************************

    bool standard_syrec_synthesizer::bitwise_negation(const std::vector<unsigned>& dest) {
        for (unsigned idx: dest) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), idx);
        }
        return true;
    }

    bool standard_syrec_synthesizer::decrement(const std::vector<unsigned>& dest) {
        for (unsigned int i: dest) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), i);
            add_active_control(i);
        }

        for (unsigned int i: dest) {
            remove_active_control(i);
        }

        return true;
        /*
    gate::line_container controls;

    for ( int i = 0; i < int( var.size() ); ++i )
    {
      append_toffoli( *( get( boost::vertex_name, cct_man.tree )[cct_man.current].circ ), controls, var.at( i ) );
      controls.insert( var.at( i ) );
    }
    */
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::decrement_additionalLineMerging(const std::vector<unsigned>& dest) {
        // Optimized version via additional lines optimization
        gate::line_container controls;
        gate::line_container helpercontrols;
        unsigned             helperline = get_constant_line(false);

        for (unsigned i = 0u; i < dest.size(); ++i) {
            if (((i % crement_merge_line_count) == 0u) && (i > 0u)) {
                if (i > crement_merge_line_count) {
                    // helperline leeren
                    append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);
                } else {
                    controls.insert(helperline);
                }
                for (unsigned j = i - crement_merge_line_count; j < i; ++j) {
                    helpercontrols.insert(dest.at(j));
                    controls.erase(dest.at(j));
                }
                append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);
            }
            append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), controls, dest.at(i));
            controls.insert(dest.at(i));
        }
        append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);
        release_constant_line(helperline, false);

        return true;
    }

    bool standard_syrec_synthesizer::increment(const std::vector<unsigned>& dest) {
        for (unsigned int i: dest) {
            add_active_control(i);
        }

        for (int i = int(dest.size()) - 1; i >= 0; --i) {
            remove_active_control(dest.at(i));
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
        }

        return true;
        /*
    gate::line_container controls( var.begin(), var.end() );

    for ( int i = (int)var.size() - 1; i >= 0; --i )
    {
      controls.erase( var.at( i ) );
      append_toffoli( *( get( boost::vertex_name, cct_man.tree )[cct_man.current].circ ), controls, var.at( i ) );
    }
    */
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::increment_additionalLineMerging(const std::vector<unsigned>& dest) {
        // Optimized Version via additional lines optimization
        gate::line_container controls;
        gate::line_container helpercontrols;
        unsigned             helperline = get_constant_line(false);

        // compute first merging for helperline
        unsigned offset = (dest.size() - 1u) - ((dest.size() - 1u) % crement_merge_line_count);
        helpercontrols.insert(dest.begin(), dest.begin() + offset);
        append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);

        controls.insert(dest.begin() + offset, dest.end());
        controls.insert(helperline);

        for (int i = (int)dest.size() - 1; i >= 0; --i) {
            controls.erase(dest.at(i));

            append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), controls, dest.at(i));

            if (((i % crement_merge_line_count) == 0) && (i > 0)) {
                // empty helperline and prepare next merging
                append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);
                for (int j = (i - (int)crement_merge_line_count); j < i; ++j) {
                    helpercontrols.erase(dest.at(j));
                    controls.insert(dest.at(j));
                }
                if (i > (int)crement_merge_line_count) {
                    // helperline computation for next merging
                    append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), helpercontrols, helperline);
                } else {
                    controls.erase(helperline);
                }
            }
        }
        release_constant_line(helperline, false);

        return true;
    }

    //**********************************************************************
    //*****                     Binary Operations                      *****
    //**********************************************************************

    bool standard_syrec_synthesizer::bitwise_and(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        bool ok = true;
        for (unsigned i = 0u; i < dest.size(); ++i) {
            ok = ok && conjunction(dest.at(i), src1.at(i), src2.at(i));
        }
        return ok;
    }

    bool standard_syrec_synthesizer::bitwise_cnot(const std::vector<unsigned>& dest, const std::vector<unsigned>& src) {
        for (unsigned i = 0u; i < src.size(); ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), dest.at(i));
        }
        return true;
    }

    bool standard_syrec_synthesizer::bitwise_or(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        bool ok = true;
        for (unsigned i = 0u; i < dest.size(); ++i) {
            ok = ok && disjunction(dest.at(i), src1.at(i), src2.at(i));
        }
        return ok;
    }

    bool standard_syrec_synthesizer::conjunction(unsigned dest, unsigned src1, unsigned src2) {
        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(src1, src2)(dest);

        return true;
    }

    bool standard_syrec_synthesizer::decrease(const std::vector<unsigned>& dest, const std::vector<unsigned>& src) {
        for (unsigned i = 0u; i < src.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
        }

        increase(dest, src);

        for (unsigned i = 0u; i < src.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
        }

        return true;
    }

    bool standard_syrec_synthesizer::decrease_with_carry(const std::vector<unsigned>& dest, const std::vector<unsigned>& src, unsigned carry) {
        for (unsigned i = 0u; i < src.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
        }

        increase_with_carry(dest, src, carry);

        for (unsigned i = 0u; i < src.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
        }

        return true;
    }

    bool standard_syrec_synthesizer::disjunction(unsigned dest, unsigned src1, unsigned src2) {
        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src1, dest);
        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2, dest);
        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(src1, src2)(dest);

        return true;
    }

    bool standard_syrec_synthesizer::division(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        // Computation of quotient and reminder (src1 is overwritten by reminder) [Code is identical with modulo]
        if (!modulo(dest, src1, src2)) return false;
        /*
    std::vector<unsigned> sum;
    std::vector<unsigned> partial;

    for ( unsigned i = 1u; i < src1.size(); ++i )
    {
      append_not( *( get( boost::vertex_name, cct_man.tree )[cct_man.current].circ ), src2.at( i ) );
    }

    for ( unsigned i = 1u; i < src1.size(); ++i )
    {
      add_active_control( src2.at( i ) );
    }

    for ( int i = int( src1.size() ) - 1; i >= 0; --i )
    {
      partial.push_back( src2.at( src1.size() - 1u - i ) );
      sum.insert( sum.begin(), src1.at( i ) );
      decrease_with_carry( sum, partial, dest.at( i ) );
      add_active_control( dest.at( i ) );
      increase( sum, partial );
      remove_active_control( dest.at( i ) );
      append_not( *( get( boost::vertex_name, cct_man.tree )[cct_man.current].circ ), dest.at( i ) );
      if ( i > 0 )
      {
        for ( unsigned j = ( src1.size() - i ); j < src1.size(); ++j )
        {
          remove_active_control( src2.at( j ) );
        }
        append_not( *( get( boost::vertex_name, cct_man.tree )[cct_man.current].circ ), src2.at( src1.size() - i ) );
        for ( unsigned j = ( src1.size() + 1u - i ); j < src1.size(); ++j )
        {
          add_active_control( src2.at( j ) );
        }
      }
    }*/

        // Back computation of first source (thereby reminder is overwritten)
        //sum.clear();
        //partial.clear();

        std::vector<unsigned> sum;
        std::vector<unsigned> partial;

        for (unsigned i = 1u; i < src1.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(i));
        }

        for (unsigned i = 1u; i < src1.size(); ++i) {
            add_active_control(src2.at(i));
        }

        for (int i = int(src1.size()) - 1; i >= 0; --i) {
            partial.push_back(src2.at(src1.size() - 1u - i));
            sum.insert(sum.begin(), src1.at(i));
            add_active_control(dest.at(i));
            increase_new(sum, partial);
            remove_active_control(dest.at(i));
            if (i > 0) {
                for (unsigned j = (src1.size() - i); j < src1.size(); ++j) {
                    remove_active_control(src2.at(j));
                }
                append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(src1.size() - i));
                for (unsigned j = (src1.size() + 1u - i); j < src1.size(); ++j) {
                    add_active_control(src2.at(j));
                }
            }
        }

        return true;
    }

    bool standard_syrec_synthesizer::equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        for (unsigned i = 0u; i < src1.size(); ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(i), src1.at(i));
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src1.at(i));
        }

        gate::line_container controls(src1.begin(), src1.end());
        append_toffoli(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), controls, dest);

        for (unsigned i = 0u; i < src1.size(); ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(i), src1.at(i));
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src1.at(i));
        }

        return true;
    }

    bool standard_syrec_synthesizer::greater_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        if (!less_than(dest, src2, src1)) return false;
        append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest);

        return true;
    }

    bool standard_syrec_synthesizer::greater_than(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        return less_than(dest, src2, src1);
    }

    bool standard_syrec_synthesizer::increase(const std::vector<unsigned>& dest, const std::vector<unsigned>& src) {
        unsigned bitwidth = src.size();

        // set module name and check whether it exists already
        std::string module_name          = "increase_" + std::to_string(bitwidth);
        bool        circ_has_module      = _circ.modules().find(module_name) != _circ.modules().end();
        bool        tree_circ_has_module = get(boost::vertex_name, cct_man.tree)[cct_man.current].circ->modules().find(module_name) != get(boost::vertex_name, cct_man.tree)[cct_man.current].circ->modules().end();

        // create module if it does not exist
        if (!circ_has_module || !tree_circ_has_module) {
            // signals src .. dest
            circuit c_increase(bitwidth * 2);

            for (unsigned i = 1U; i < bitwidth; ++i) {
                append_cnot(c_increase, i, bitwidth + i);
            }

            if (bitwidth >= 2u) {
                for (unsigned i = bitwidth - 2U; i > 0; --i) {
                    append_cnot(c_increase, i, i + 1);
                }
            }

            for (unsigned i = 0U; i < bitwidth - 1U; ++i) {
                append_toffoli(c_increase)(i, bitwidth + i)(i + 1);
            }

            for (unsigned i = bitwidth - 1U; i > 0; --i) {
                append_cnot(c_increase, i, bitwidth + i);
                append_toffoli(c_increase)(bitwidth + i - 1, i - 1)(i);
            }

            if (bitwidth >= 2u) {
                for (unsigned i = 1U; i < bitwidth - 1U; ++i) {
                    append_cnot(c_increase, i, i + 1);
                }
            }

            for (unsigned i = 0U; i < bitwidth; ++i) {
                append_cnot(c_increase, i, bitwidth + i);
            }

            if (!circ_has_module) {
                _circ.add_module(module_name, c_increase);
            }

            if (!tree_circ_has_module) {
                get(boost::vertex_name, cct_man.tree)[cct_man.current].circ->add_module(module_name, c_increase);
            }
        }

        std::vector<unsigned> targets{src.begin(), src.end()};
        targets.insert(targets.end(), dest.begin(), dest.end());
        append_module(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), module_name, gate::line_container(), targets);
        return true;
    }
    //end of increase

    ////////////////////////////////NEW INCREASE FUNCTION /////////
    [[maybe_unused]] bool standard_syrec_synthesizer::maj_2(unsigned in1, unsigned in2) {
        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in2, in1);
        return true;
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::maj(unsigned in1, unsigned in2, unsigned in3) {
        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in3, in2);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in3, in1);

        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(in1, in2)(in3);

        return true;
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::uma(unsigned in1, unsigned in2, unsigned in3) {
        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(in1, in2)(in3);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in3, in1);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in1, in2);
        return true;
    }

    [[maybe_unused]] bool standard_syrec_synthesizer::uma_3cnot(unsigned in1, unsigned in2, unsigned in3) {
        append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in2);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in1, in2);

        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(in1, in2)(in3);

        append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in2);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in3, in1);

        append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), in3, in2);
        return true;
    }

    bool standard_syrec_synthesizer::increase_new(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs) {
        unsigned bitwidth = rhs.size();

        if (bitwidth == 1) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(0), rhs.at(0));
        } else {
            for (unsigned i = 1; i <= bitwidth - 1; ++i) {
                append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(i), rhs.at(i));
            }
            for (unsigned i = bitwidth - 2; i >= 1; --i) {
                append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(i), lhs.at(i + 1));
            }
            for (unsigned i = 0; i <= bitwidth - 2; ++i) {
                append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(rhs.at(i), lhs.at(i))(lhs.at(i + 1));
            }

            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(bitwidth - 1), rhs.at(bitwidth - 1));

            for (unsigned i = bitwidth - 2; i >= 1; --i) {
                append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(lhs.at(i), rhs.at(i))(lhs.at(i + 1));
                append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(i), rhs.at(i));
            }
            append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(lhs.at(0), rhs.at(0))(lhs.at(1));
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(0), rhs.at(0));

            for (unsigned i = 1; i <= bitwidth - 2; ++i) {
                append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(i), lhs.at(i + 1));
            }
            for (unsigned i = 1; i <= bitwidth - 1; ++i) {
                append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lhs.at(i), rhs.at(i));
            }
        }

        return true;
    }

    bool standard_syrec_synthesizer::decrease_new(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs) {
        for (unsigned int rh: rhs) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), rh);
        }

        increase_new(rhs, lhs);

        for (unsigned int rh: rhs) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), rh);
        }
        return true;
    }

    bool standard_syrec_synthesizer::decrease_new_assign(const std::vector<unsigned>& rhs, const std::vector<unsigned>& lhs) {
        for (unsigned int lh: lhs) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lh);
        }

        increase_new(rhs, lhs);

        for (unsigned int lh: lhs) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), lh);
        }

        for (unsigned i = 0u; i < lhs.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), rhs.at(i));
        }
        return true;
    }

    bool standard_syrec_synthesizer::expression_op_inverse(unsigned op, const std::vector<unsigned>& exp_lhs, const std::vector<unsigned>& exp_rhs) {
        switch (op) {
            case applications::binary_expression::add: // +
            {
                decrease_new(exp_rhs, exp_lhs);
            } break;

            case applications::binary_expression::subtract: // -
            {
                decrease_new_assign(exp_rhs, exp_lhs);
            } break;
            case applications::binary_expression::exor: // ^
            {
                bitwise_cnot(exp_rhs, exp_lhs);
            } break;

            default: return false;
        }
        return true;
    }

    bool standard_syrec_synthesizer::expression_single_op(unsigned op, const std::vector<unsigned>& exp_lhs, const std::vector<unsigned>& exp_rhs) {
        switch (op) {
            case applications::binary_expression::add: // +
            {
                increase_new(exp_rhs, exp_lhs);
            } break;

            case applications::binary_expression::subtract: // -
            {
                if (sub_flag) {
                    decrease_new_assign(exp_rhs, exp_lhs);
                } else {
                    decrease_new(exp_rhs, exp_lhs);
                }
            } break;
            case applications::binary_expression::exor: // ^
            {
                bitwise_cnot(exp_rhs, exp_lhs);
            } break;

            default: return false;
        }
        return true;
    }

    bool standard_syrec_synthesizer::increase_with_carry(const std::vector<unsigned>& dest, const std::vector<unsigned>& src, unsigned carry) {
        unsigned bitwidth = src.size();

        if (bitwidth == 0) return true;

        for (unsigned i = 1u; i < bitwidth; ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), dest.at(i));
        }

        if (bitwidth > 1) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(bitwidth - 1), carry);
        }
        for (int i = (int)bitwidth - 2; i > 0; --i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), src.at(i + 1));
        }

        for (unsigned i = 0u; i < bitwidth - 1; ++i) {
            append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(src.at(i), dest.at(i))(src.at(i + 1));
        }
        append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(src.at(bitwidth - 1), dest.at(bitwidth - 1))(carry);

        for (int i = (int)bitwidth - 1; i > 0; --i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), dest.at(i));
            append_toffoli (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))(dest.at(i - 1), src.at(i - 1))(src.at(i));
        }

        for (unsigned i = 1u; i < bitwidth - 1u; ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), src.at(i + 1));
        }

        for (unsigned i = 0u; i < bitwidth; ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src.at(i), dest.at(i));
        }

        return true;
    }

    bool standard_syrec_synthesizer::less_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        if (!greater_than(dest, src2, src1)) return false;
        append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest);

        return true;
    }

    bool standard_syrec_synthesizer::less_than(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        return (decrease_with_carry(src1, src2, dest) && increase(src1, src2));
    }

    bool standard_syrec_synthesizer::modulo(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        std::vector<unsigned> sum;
        std::vector<unsigned> partial;

        for (unsigned i = 1u; i < src1.size(); ++i) {
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(i));
        }

        for (unsigned i = 1u; i < src1.size(); ++i) {
            add_active_control(src2.at(i));
        }

        for (int i = int(src1.size()) - 1; i >= 0; --i) {
            partial.push_back(src2.at(src1.size() - 1u - i));
            sum.insert(sum.begin(), src1.at(i));
            decrease_with_carry(sum, partial, dest.at(i));
            add_active_control(dest.at(i));
            increase_new(sum, partial);
            remove_active_control(dest.at(i));
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest.at(i));
            if (i > 0) {
                for (unsigned j = (src1.size() - i); j < src1.size(); ++j) {
                    remove_active_control(src2.at(j));
                }
                append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src2.at(src1.size() - i));
                for (unsigned j = (src1.size() + 1u - i); j < src1.size(); ++j) {
                    add_active_control(src2.at(j));
                }
            }
        }
        return true;
    }

    bool standard_syrec_synthesizer::multiplication(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        /* std::vector<unsigned> sum = dest;
    std::vector<unsigned> partial = src2;

    bool ok = true;

    for ( unsigned i = 0; i < dest.size(); ++i )
    {
      add_active_control( src1.at( i ) );
      ok = ok && increas( sum, partial );
      remove_active_control( src1.at( i ) );
      sum.erase(sum.begin());
      partial.pop_back();
    }

    return ok;*/
        if ((src1.empty()) || (dest.empty())) return true;

        std::vector<unsigned> sum     = dest;
        std::vector<unsigned> partial = src2;

        bool ok = true;

        add_active_control(src1.at(0));
        ok = ok && bitwise_cnot(sum, partial);
        remove_active_control(src1.at(0));

        for (unsigned i = 1; i < dest.size(); ++i) {
            sum.erase(sum.begin());
            partial.pop_back();
            add_active_control(src1.at(i));
            ok = ok && increase_new(sum, partial);
            remove_active_control(src1.at(i));
        }

        return ok;
    }

    // dest.size = 2 * srcX.size
    bool standard_syrec_synthesizer::multiplication_full(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        std::vector<unsigned>        sum(dest.begin(), dest.begin() + (unsigned)src2.size());
        const std::vector<unsigned>& partial = src2;

        bool ok = true;

        for (unsigned i = 0; i < dest.size(); ++i) {
            add_active_control(src1.at(i));
            ok = ok && increase(sum, partial);
            remove_active_control(src1.at(i));
            sum.erase(sum.begin());
            sum.push_back(dest.at(src2.size() + i));
        }

        return ok;
    }

    bool standard_syrec_synthesizer::not_equals(unsigned dest, const std::vector<unsigned>& src1, const std::vector<unsigned>& src2) {
        if (!equals(dest, src1, src2)) return false;
        append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), dest);
        return true;
    }

    bool standard_syrec_synthesizer::swap(const std::vector<unsigned>& dest1, const std::vector<unsigned>& dest2) {
        for (unsigned i = 0u; i < dest1.size(); ++i) {
            append_fredkin (*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ))()(dest1.at(i), dest2.at(i));
        }
        return true;
    }

    //**********************************************************************
    //*****                      Shift Operations                      *****
    //**********************************************************************

    bool standard_syrec_synthesizer::left_shift(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, unsigned src2) {
        for (unsigned i = 0u; (i + src2) < dest.size(); ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src1.at(i), dest.at(i + src2));
        }
        return true;
    }

    bool standard_syrec_synthesizer::right_shift(const std::vector<unsigned>& dest, const std::vector<unsigned>& src1, unsigned src2) {
        for (unsigned i = src2; i < dest.size(); ++i) {
            append_cnot(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), src1.at(i), dest.at(i - src2));
        }
        return true;
    }

    //**********************************************************************
    //*****                     Efficient Controls                     *****
    //**********************************************************************

    bool standard_syrec_synthesizer::add_active_control(unsigned control) {
        // aktuelles Blatt vollendet, zurueck zum parent
        cct_man.current = source(*(in_edges(cct_man.current, cct_man.tree).first), cct_man.tree);

        // child fuer neuen control anlegen
        cct_node child                                        = add_vertex(cct_man.tree);
        get(boost::vertex_name, cct_man.tree)[child].control  = control;
        get(boost::vertex_name, cct_man.tree)[child].controls = get(boost::vertex_name, cct_man.tree)[cct_man.current].controls;
        get(boost::vertex_name, cct_man.tree)[child].controls.insert(control);
        // get( boost::vertex_name, cct_man.tree )[child].circ = std::shared_ptr<circuit>( new circuit() );
        add_edge(cct_man.current, child, cct_man.tree);
        cct_man.current = child;

        // neues Blatt anlegen
        cct_node leaf                                        = add_vertex(cct_man.tree);
        get(boost::vertex_name, cct_man.tree)[leaf].controls = get(boost::vertex_name, cct_man.tree)[cct_man.current].controls;
        get(boost::vertex_name, cct_man.tree)[leaf].circ     = std::make_shared<circuit>();
        get(boost::vertex_name, cct_man.tree)[leaf].circ->gate_added.connect(annotater(*get(boost::vertex_name, cct_man.tree)[leaf].circ, _stmts));
        add_edge(cct_man.current, leaf, cct_man.tree);
        cct_man.current = leaf;

        return true;
    }

    bool standard_syrec_synthesizer::remove_active_control(unsigned control [[maybe_unused]]) {
        // aktuelles Blatt vollendet, zurueck zum parent
        cct_man.current = source(*(in_edges(cct_man.current, cct_man.tree).first), cct_man.tree);

        // aktueller Knoten abgeschlossen, zurueck zum parent
        cct_man.current = source(*(in_edges(cct_man.current, cct_man.tree).first), cct_man.tree);

        // neues Blatt anlegen
        cct_node leaf                                        = add_vertex(cct_man.tree);
        get(boost::vertex_name, cct_man.tree)[leaf].controls = get(boost::vertex_name, cct_man.tree)[cct_man.current].controls;
        get(boost::vertex_name, cct_man.tree)[leaf].circ     = std::make_shared<circuit>();
        get(boost::vertex_name, cct_man.tree)[leaf].circ->gate_added.connect(annotater(*get(boost::vertex_name, cct_man.tree)[leaf].circ, _stmts));
        add_edge(cct_man.current, leaf, cct_man.tree);
        cct_man.current = leaf;
        return true;
    }

    bool standard_syrec_synthesizer::assemble_circuit(const cct_node& current) {
        // leaf
        if (out_edges(current, cct_man.tree).first == out_edges(current, cct_man.tree).second /*get( boost::vertex_name, cct_man.tree )[current].circ.get()->num_gates() > 0u*/) {
            append_circuit(_circ, *(get(boost::vertex_name, cct_man.tree)[current].circ), get(boost::vertex_name, cct_man.tree)[current].controls);
            return true;
        }

        if (optimization_decision(current)) {
            // optimize the controlled cascade of the current node
            unsigned             helper_line = get_constant_line(false);
            gate::line_container controls;
            controls.insert(helper_line);
            append_toffoli(_circ, get(boost::vertex_name, cct_man.tree)[current].controls, helper_line);
            for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
                if (!assemble_circuit(_circ, target(*edge_it, cct_man.tree), controls)) return false;
            }
            append_toffoli(_circ, get(boost::vertex_name, cct_man.tree)[current].controls, helper_line);
            release_constant_line(helper_line, false);
        } else {
            // assemble optimized circuits of successors
            for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
                if (!assemble_circuit(target(*edge_it, cct_man.tree))) return false;
            }
        }
        return true;
    }

    bool standard_syrec_synthesizer::assemble_circuit(circuit& circ, const cct_node& current, gate::line_container controls) {
        // leaf
        if (out_edges(current, cct_man.tree).first == out_edges(current, cct_man.tree).second /*get( boost::vertex_name, cct_man.tree )[current].circ->num_gates() > 0*/) {
            append_circuit(circ, *(get(boost::vertex_name, cct_man.tree)[current].circ), controls);
            return true;
        }

        controls.insert(get(boost::vertex_name, cct_man.tree)[current].control);
        for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
            if (!assemble_circuit(circ, target(*edge_it, cct_man.tree), controls)) return false;
        }
        return true;
    }

    bool standard_syrec_synthesizer::optimization_decision(const cct_node& current) {
        if (efficient_controls) {
            return (optimizationCost(current) == bestCost(current));
        }
        return false;
    }

    unsigned standard_syrec_synthesizer::bestCost(const cct_node& current) {
        const auto stdCost = standardCost(current, get(boost::vertex_name, cct_man.tree)[current].controls.size());

        // leaf
        if (out_edges(current, cct_man.tree).first == out_edges(current, cct_man.tree).second /*get( boost::vertex_name, cct_man.tree )[current].circ->num_gates() > 0*/) {
            return stdCost;
        }

        const auto optCost  = optimizationCost(current);
        const auto succCost = successorsCost(current);

        return (std::min)({stdCost, optCost, succCost});
    }

    unsigned standard_syrec_synthesizer::standardCost(const cct_node& current [[maybe_unused]], unsigned controls [[maybe_unused]]) {
        unsigned cost = 0u;
        //        quantum_costs qc;
        //        qc.controls_offset = controls;

        // leaf
        //        if (out_edges(current, cct_man.tree).first == out_edges(current, cct_man.tree).second /*get( boost::vertex_name, cct_man.tree )[current].circ->num_gates() > 0*/) {
        //            cost += costs(*(get(boost::vertex_name, cct_man.tree)[current].circ), costs_by_gate_func(qc));
        //        } else {
        //            for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
        //               cost += standardCost(target(*edge_it, cct_man.tree), controls + 1u);
        //           }
        //        }

        return cost;
    }

    unsigned standard_syrec_synthesizer::optimizationCost(const cct_node& current [[maybe_unused]]) {
        unsigned cost = 1u;

        //        quantum_costs qc;
        //        qc.controls_offset = get(boost::vertex_name, cct_man.tree)[current].controls.size();

        //        circuit tmp_circ;
        //        append_not(tmp_circ, 1u);
        //        cost += 2 * costs(tmp_circ, costs_by_gate_func(qc));

        //        for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
        //            cost += standardCost(target(*edge_it, cct_man.tree), 1u);
        //        }

        return cost;
    }

    unsigned standard_syrec_synthesizer::successorsCost(const cct_node& current [[maybe_unused]]) {
        unsigned cost = 1u;

        //        for (boost::graph_traits<cct>::out_edge_iterator edge_it = out_edges(current, cct_man.tree).first; edge_it != out_edges(current, cct_man.tree).second; ++edge_it) {
        //            cost += bestCost(target(*edge_it, cct_man.tree));
        //        }

        return cost;
    }

    circuit& standard_syrec_synthesizer::circ() const {
        return _circ;
    }

    [[maybe_unused]] std::stack<applications::statement::ptr>& standard_syrec_synthesizer::stmts() {
        return _stmts;
    }

    template<typename T>
    struct is_type {
        template<typename Ptr>
        bool operator()(const Ptr& p) const {
            return dynamic_cast<T*>(p.get());
        }
    };

    bool standard_syrec_synthesizer::get_variables(applications::variable_access::ptr var, std::vector<unsigned>& lines) {
        if (dupl_if_var_mapping.find(var) != dupl_if_var_mapping.end()) {
            var = dupl_if_var_mapping.find(var)->second;
        }

        unsigned offset = _var_lines[var->var()];

        if (!var->indexes().empty()) {
            // change offset

            // check if it is all numeric_expressions
            unsigned n = var->var()->dimensions().size(); // dimensions
            if ((unsigned)std::count_if(var->indexes().cbegin(), var->indexes().cend(), is_type<applications::numeric_expression>()) == n) {
                for (unsigned i = 0u; i < n; ++i) {
                    offset += dynamic_cast<applications::numeric_expression*>(var->indexes().at(i).get())->value()->evaluate(loop_map) *
                              std::accumulate(var->var()->dimensions().begin() + i + 1u, var->var()->dimensions().end(), 1u, std::multiplies<>()) *
                              var->var()->bitwidth();
                }
            } else {
                get_constant_lines(var->var()->bitwidth(), 0u, lines);
                if (!array_swapping(offset, var->var()->dimensions(), var->indexes(), var->var()->bitwidth(), lines)) return false;
                return true;
            }
        }

        if (var->range()) {
            auto [nfirst, nsecond] = *var->range();

            unsigned first  = nfirst->evaluate(loop_map);
            unsigned second = nsecond->evaluate(loop_map);

            if (first < second) {
                for (unsigned i = first; i <= second; ++i) {
                    lines.emplace_back(offset + i);
                }
            } else {
                for (int i = (int)first; i >= (int)second; --i) {
                    lines.emplace_back(offset + i);
                }
            }
        } else {
            for (unsigned i = 0u; i < var->var()->bitwidth(); ++i) {
                lines.emplace_back(offset + i);
            }
        }
        return true;
    }

    bool standard_syrec_synthesizer::unget_variables(const applications::variable_access::ptr& var, std::vector<unsigned>& lines) {
        unsigned offset = _var_lines[var->var()];

        if (!var->indexes().empty()) {
            // change offset

            // check if it is not all numeric_expressions
            unsigned n = var->var()->dimensions().size(); // dimensions
            if ((unsigned)std::count_if(var->indexes().cbegin(), var->indexes().cend(), is_type<applications::numeric_expression>()) != n) {
                array_swapping(offset, var->var()->dimensions(), var->indexes(), var->var()->bitwidth(), lines);
            }
        }

        return true;
    }

    // Helper function
    unsigned getLowestOneBit(unsigned number) {
        for (unsigned i = 0u; number > 0; ++i) {
            if (number % 2) return i;
            number /= 2;
        }
        return 0;
    }

    /**
   * Function to access array variables
   *
   * The array variable that corresponds to the given indexes is exchanged (via swap operations) with some given helper lines
   *
   * \param offset is the first line number associated to the array
   * \param dimensions is the dimensions of the array
   * \param indexes is the indexes of the array
   * \param bitwidth is the bitwidth of the variables within the array
   * \param lines is the destination, where
   */
    bool standard_syrec_synthesizer::array_swapping(unsigned offset, std::vector<unsigned> dimensions, std::vector<std::shared_ptr<applications::expression>> indexes, unsigned bitwidth, std::vector<unsigned>& lines) {
        unsigned op = 0u;
        if (indexes.empty()) {
            std::vector<unsigned> dest_lines;
            for (unsigned i = 0; i < bitwidth; ++i) {
                dest_lines.emplace_back(offset + i);
            }
            return swap(dest_lines, lines);
        }

        if (dynamic_cast<applications::numeric_expression*>(indexes.at(0).get())) //( is_type<applications::numeric_expression>( indexes.at( 0 ) ) )
        {
            offset += dynamic_cast<applications::numeric_expression*>(indexes.at(0).get())->value()->evaluate(loop_map) *
                      std::accumulate(dimensions.begin() + 1u, dimensions.end(), 1u, std::multiplies<>()) *
                      bitwidth;
            dimensions.erase(dimensions.begin());
            indexes.erase(indexes.begin());
            array_swapping(offset, dimensions, indexes, bitwidth, lines);
        } else {
            unsigned dimension = dimensions.at(0);
            dimensions.erase(dimensions.begin());
            std::vector<unsigned>             select_lines, lhs_stat;
            std::stack<unsigned>              exp_op1;
            std::stack<std::vector<unsigned>> exp_lhs1, exp_rhs1;
            if (!on_expression(indexes.at(0), select_lines, lhs_stat, op)) return false;
            indexes.erase(indexes.begin());

            unsigned current_subarray = dimension - 1u;
            for (unsigned i = 1u; i <= dimension; ++i) {
                // activate controls (select_lines)
                for (unsigned int select_line: select_lines) {
                    add_active_control(select_line);
                }

                if (!array_swapping(offset + current_subarray * std::accumulate(dimensions.begin(), dimensions.end(), 1u, std::multiplies<>()) *
                                                     bitwidth,
                                    dimensions, indexes, bitwidth, lines)) return false;

                // deactivate controls
                for (unsigned int select_line: select_lines) {
                    remove_active_control(select_line);
                }

                append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), select_lines.at((i < dimension ? getLowestOneBit(i) : (select_lines.size() - 1u))));
                current_subarray ^= unsigned(pow(2, getLowestOneBit(i)));
            }
        }
        return true;
    }

    unsigned standard_syrec_synthesizer::get_constant_line(bool value) {
        unsigned const_line = 0u;

        if (!free_const_lines_map[value].empty()) {
            const_line = free_const_lines_map[value].back();
            free_const_lines_map[value].pop_back();
        } else if (!free_const_lines_map[!value].empty()) {
            const_line = free_const_lines_map[!value].back();
            free_const_lines_map[!value].pop_back();
            append_not(*(get(boost::vertex_name, cct_man.tree)[cct_man.current].circ), const_line);
        } else {
            const_line = add_line_to_circuit(_circ, (std::string("const_") + boost::lexical_cast<std::string>(!value)), "garbage", value, true);
        }

        return const_line;
    }

    bool standard_syrec_synthesizer::get_constant_lines(unsigned bitwidth, unsigned value, std::vector<unsigned>& lines) {
        boost::dynamic_bitset<> number(bitwidth, value);

        for (unsigned i = 0u; i < bitwidth; ++i) {
            lines.emplace_back(get_constant_line(number.test(i)));
        }

        return true;
    }

    void standard_syrec_synthesizer::release_constant_line(unsigned index, bool value) {
        free_const_lines_map[value].push_back(index);

        // output description is changed to constant;
        std::vector<std::string> outs = _circ.outputs();
        outs.at(index)                = (std::string("const_") + boost::lexical_cast<std::string>(value));
        _circ.set_outputs(outs);
    }

    standard_syrec_synthesizer::var_lines_map& standard_syrec_synthesizer::var_lines() {
        return _var_lines;
    }

    // helper function
    void _add_variable(circuit& circ, const std::vector<unsigned>& dimensions, const applications::variable::ptr& var, const std::string& variable_name_format,
                       constant _constant, bool _garbage, const std::string& arraystr) {
        if (dimensions.empty()) {
            for (unsigned i = 0u; i < var->bitwidth(); ++i) {
                std::string name = boost::str(boost::format(variable_name_format) % var->name() % i % arraystr);
                add_line_to_circuit(circ, name, name, _constant, _garbage);
            }

            // busses
            std::vector<unsigned> affected_lines;
            for (std::size_t i = circ.lines() - var->bitwidth(); i < circ.lines(); ++i) {
                affected_lines.emplace_back(i);
            }
            std::string name = var->name() + arraystr;

            if (var->type() == applications::variable::in || var->type() == applications::variable::inout) {
                circ.inputbuses().add(name, affected_lines);
            }
            if (var->type() == applications::variable::out || var->type() == applications::variable::inout) {
                circ.outputbuses().add(name, affected_lines);
            }
            if (var->type() == applications::variable::state) {
                circ.statesignals().add(name, affected_lines);
            }
        } else {
            unsigned              len = dimensions.front();
            std::vector<unsigned> new_dimensions(dimensions.begin() + 1u, dimensions.end());

            for (unsigned i = 0u; i < len; ++i) {
                _add_variable(circ, new_dimensions, var, variable_name_format,
                              _constant, _garbage, arraystr + "[" + std::to_string(i) + "]");
            }
        }
    }

    void add_variables(circuit& circ, standard_syrec_synthesizer& synthesizer, const std::string& variable_name_format, const applications::variable::vec& variables) {
        for (const applications::variable::ptr& var: variables) {
            // entry in var lines map
            synthesizer.var_lines().insert(std::make_pair(var, circ.lines()));

            // types of constant and garbage
            constant _constant = (var->type() == applications::variable::out || var->type() == applications::variable::wire) ? constant(true) : constant(false);
            bool     _garbage  = (var->type() == applications::variable::in || var->type() == applications::variable::wire);

            _add_variable(circ, var->dimensions(), var, variable_name_format,
                          _constant, _garbage, std::string());
        }
    }

    bool syrec_synthesis(circuit& circ, const applications::program& program, const properties::ptr& settings, const properties::ptr& statistics) {
        // Settings parsing
        auto variable_name_format  = get<std::string>(settings, "variable_name_format", "%1$s%3$s.%2$d");
        auto main_module           = get<std::string>(settings, "main_module", std::string());
        auto statement_synthesizer = get<standard_syrec_synthesizer>(settings, "statement_synthesizer", standard_syrec_synthesizer(circ, program));

        statement_synthesizer.set_settings(settings);
        //    std::ostream& warning_stream = get<std::ostream&>( settings, "warning_stream", std::cout );

        // Run-time measuring
        timer<properties_timer> t;

        if (statistics) {
            properties_timer rt(statistics);
            t.start(rt);
        }

        statement_synthesizer.initialize_changing_variables(program);

        // get the main module
        applications::module::ptr main;

        if (!main_module.empty()) {
            main = program.find_module(main_module);
            if (!main) {
                std::cerr << "Program has no module: " << main_module << std::endl;
                return false;
            }
        } else {
            main = program.find_module("main");
            if (!main) {
                main = program.modules().front();
            }
        }

        // declare as top module
        statement_synthesizer.set_main_module(main);

        // create lines for global variables
        add_variables(circ, statement_synthesizer, variable_name_format, main->parameters());
        add_variables(circ, statement_synthesizer, variable_name_format, main->variables());

        // synthesize the statements
        return statement_synthesizer.on_module(main);
        //write_realization( circ,std::cout); //tanmay
    }

    void standard_syrec_synthesizer::initialize_changing_variables(const applications::program& program) {
        // Compute changed variable map for if_realization via duplication
        if (if_realization == syrec_synthesis_if_realization_duplication) {
            compute_changing_variables(program, _changing_variables);
        }
    }

    void standard_syrec_synthesizer::compute_changing_variables(const applications::program& program, std::map<const applications::statement*, var_set>& changing_variables) {
        for (const applications::module::ptr& mod: program.modules()) {
            compute_changing_variables(mod, changing_variables);
        }
    }

    void standard_syrec_synthesizer::compute_changing_variables(const applications::module::ptr& module, std::map<const applications::statement*, var_set>& changing_variables) {
        for (const applications::statement::ptr& stat: module->statements()) {
            compute_changing_variables(stat, changing_variables);
        }
    }

    void standard_syrec_synthesizer::compute_changing_variables(const applications::statement::ptr& statement, std::map<const applications::statement*, var_set>& changing_variables) {
        var_set changed_variables;
        if (auto* stat = dynamic_cast<applications::swap_statement*>(statement.get())) {
            changed_variables.insert(stat->lhs());
            changed_variables.insert(stat->rhs());
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_8 = dynamic_cast<applications::unary_statement*>(statement.get())) {
            changed_variables.insert(stat_8->var());
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_9 = dynamic_cast<applications::assign_statement*>(statement.get())) {
            changed_variables.insert(stat_9->lhs());
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_10 = dynamic_cast<applications::if_statement*>(statement.get())) {
            for (const applications::statement::ptr& s: stat_10->then_statements()) {
                compute_changing_variables(s, changing_variables);
                changed_variables.insert(changing_variables.find(s.get())->second.begin(), changing_variables.find(s.get())->second.end());
            }
            for (const applications::statement::ptr& s: stat_10->else_statements()) {
                compute_changing_variables(s, changing_variables);
                changed_variables.insert(changing_variables.find(s.get())->second.begin(), changing_variables.find(s.get())->second.end());
            }
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_11 = dynamic_cast<applications::for_statement*>(statement.get())) {
            for (const applications::statement::ptr& s: stat_11->statements()) {
                compute_changing_variables(s, changing_variables);
                changed_variables.insert(changing_variables.find(s.get())->second.begin(), changing_variables.find(s.get())->second.end());
            }
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_12 = dynamic_cast<applications::call_statement*>(statement.get())) {
            for (const applications::statement::ptr& s: stat_12->target()->statements()) {
                compute_changing_variables(s, changing_variables);
                changed_variables.insert(changing_variables.find(s.get())->second.begin(), changing_variables.find(s.get())->second.end());
            }
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (auto* stat_13 = dynamic_cast<applications::uncall_statement*>(statement.get())) {
            for (const applications::statement::ptr& s: stat_13->target()->statements()) {
                compute_changing_variables(s, changing_variables);
                changed_variables.insert(changing_variables.find(s.get())->second.begin(), changing_variables.find(s.get())->second.end());
            }
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else if (/*applications::skip_statement* stat = */ dynamic_cast<applications::skip_statement*>(statement.get())) {
            // changing_variables.insert( std::make_pair( stat, changed_variables ) );
            // return;
        } else {
            return;
        }
        changing_variables.insert(std::make_pair(statement.get(), changed_variables));
    }

    [[maybe_unused]] hdl_synthesis_func syrec_synthesis_func(const properties::ptr& settings, const properties::ptr& statistics) {
        hdl_synthesis_func f = [settings, statistics](auto&& PH1, auto&& PH2) { return syrec_synthesis(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), settings, statistics); };
        f.init(settings, statistics);
        return f;
    }

} // namespace syrec
