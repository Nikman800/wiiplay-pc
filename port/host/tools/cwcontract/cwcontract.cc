/* cwcontract: CodeWarrior's floating-point contraction, for GCC (port/docs/HOST_BUILD.md §43).
 *
 * CodeWarrior with -fp_contract on (the game's units and EGG) fuses a multiply into the add
 * or subtract that takes it: fmadd(s), fmsub(s), fnmsub(s), fnmadd(s), one rounding where C
 * rounds twice. GCC's own contraction (-ffp-contract=fast) needs the FMA instructions at
 * compile time and fuses in other places (after inlining, across statements, after CSE).
 * This plugin makes CodeWarrior's decisions instead, in the parsed source, and writes each
 * fused operation as a call to the port's exact fused multiply-add (port_fmaf / port_fma,
 * src/host_fma.c: vfmadd when the CPU has it, else an exact software form), so the code
 * runs on every x86-64 CPU with the same bits.
 *
 *   -fplugin=cwcontract.so -fplugin-arg-cwcontract-default=on|off
 *        [-fplugin-arg-cwcontract-report=FILE]   (one line per fused operation)
 *
 * The rule, from CodeWarrior's own output (GC/3.0a5.2, -O4,p; §43): within one expression,
 * an add or subtract one of whose operands is directly a multiply of the same type fuses
 * with it; the left operand when both are multiplies:
 *   a*b + c, c + a*b  ->  fmadd   fma(a, b, c)
 *   a*b - c           ->  fmsub   fma(a, b, -c)
 *   c - a*b           ->  fnmsub  -fma(a, b, -c)    (Broadway's -(a*b - c), sign of zero too)
 *   x += a*b, x -= a*b  likewise (x = x + a*b).
 * Nothing fuses through a variable (t = a*b; t + c), a cast ((f32)(a*b) + c), a negation of
 * the product (-(a*b) + c), a conversion to another type (a*b + 1.0), or an inline
 * function's return value (mul(a, b) + c). Negated operands stay where they are
 * ((-a)*b + c is fma(-a, b, c)). That is the expression as written, so the plugin works on
 * each function's tree as parsed (PLUGIN_PRE_GENERICIZE), before GCC folds anything:
 * GCC's folder turns -(a*b) + c into c - a*b and drops casts.
 *
 * `#pragma fp_contract on|off` switches it for what follows in the translation unit, as in
 * CodeWarrior: an expression takes the state at its position in the token stream (headers
 * included; an inline function its own position). The unit is compiled with
 * -ffp-contract=off, so GCC itself fuses nothing.
 */
/* the C++ library before GCC's headers (system.h poisons some names) */
#include <string>
#include <utility>
#include <vector>

#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "tree-iterator.h"
#include "stringpool.h"
#include "attribs.h"
#include "c-family/c-common.h"
#include "c-family/c-pragma.h"
#include "diagnostic-core.h"
#include "input.h"
#include "tree-pretty-print.h"


int plugin_is_GPL_compatible;

static bool contract_default = false;
static std::vector<std::pair<location_t, bool>> pragmas; /* in token order */
static FILE* report = NULL;
static tree current_fn = NULL_TREE;
static hash_set<tree>* own_negations; /* the negated addends fuse() makes */
static std::vector<std::string> excluded; /* directories whose code never fuses */

/* The port's own headers (host forms of the SDK's and nw4r's asm, which fuse exactly
   where they say so) and the system headers are not CodeWarrior's code. */
static bool excluded_at(location_t loc) {
    location_t sp = linemap_resolve_location(line_table, LOCATION_LOCUS(loc), LRK_SPELLING_LOCATION, NULL);
    if (sp == UNKNOWN_LOCATION) {
        return false;
    }
    if (in_system_header_at(sp)) {
        return true;
    }
    const char* f = LOCATION_FILE(sp);
    if (!f) {
        return false;
    }
    for (auto& d : excluded) {
        if (!strncmp(f, d.c_str(), d.size())) {
            return true;
        }
    }
    return false;
}

static location_t ordinary(location_t loc) {
    loc = LOCATION_LOCUS(loc);
    if (loc == UNKNOWN_LOCATION) {
        return loc;
    }
    return linemap_resolve_location(line_table, loc, LRK_MACRO_EXPANSION_POINT, NULL);
}

static bool contract_at(location_t loc) {
    loc = ordinary(loc);
    bool on = contract_default;
    if (loc == UNKNOWN_LOCATION) {
        return on;
    }
    for (auto& p : pragmas) {
        if (p.first > loc) {
            break;
        }
        on = p.second;
    }
    return on;
}

static void handle_fp_contract(cpp_reader*) {
    tree x;
    location_t loc = UNKNOWN_LOCATION;
    enum cpp_ttype t = pragma_lex(&x, &loc);
    if (t == CPP_NAME) {
        const char* s = IDENTIFIER_POINTER(x);
        if (!strcmp(s, "on") || !strcmp(s, "off")) {
            pragmas.emplace_back(ordinary(loc), s[1] == 'n');
            return;
        }
    }
    warning_at(loc, 0, "%<#pragma fp_contract%>: expected on or off");
}

static void register_pragmas(void*, void*) {
    c_register_pragma(NULL, "fp_contract", handle_fp_contract);
}

static bool float_type_p(tree type) {
    return type && TREE_CODE(type) == REAL_TYPE &&
           (TYPE_MODE(type) == SFmode || TYPE_MODE(type) == DFmode);
}

/* port/cw_fma.h's inline port_cw_fmaf / port_cw_fma when the unit has them (force-included),
   else the out-of-line port_fmaf / port_fma (src/host_fma.c). */
static tree fma_decl(tree type) {
    static tree decls[2];
    int dbl = TYPE_MODE(type) == DFmode;
    if (!decls[dbl]) {
        tree d = identifier_global_value(get_identifier(dbl ? "port_cw_fma" : "port_cw_fmaf"));
        if (!d || TREE_CODE(d) != FUNCTION_DECL) {
            tree t = dbl ? double_type_node : float_type_node;
            const char* name = dbl ? "port_fma" : "port_fmaf";
            d = build_fn_decl(name, build_function_type_list(t, t, t, t, NULL_TREE));
            SET_DECL_ASSEMBLER_NAME(d, get_identifier(name));
            TREE_READONLY(d) = 1; /* const: no memory; the same bits on every path */
            TREE_NOTHROW(d) = 1;
            DECL_ATTRIBUTES(d) = tree_cons(get_identifier("leaf"), NULL_TREE, NULL_TREE);
        }
        decls[dbl] = d;
    }
    return decls[dbl];
}

static bool real_cst_p(tree x, const REAL_VALUE_TYPE& v) {
    x = tree_strip_any_location_wrapper(x);
    return TREE_CODE(x) == REAL_CST && real_identical(TREE_REAL_CST_PTR(x), &v);
}

/* x / 2^k (k >= 1): CodeWarrior multiplies by the reciprocal (exact), and that multiply
   fuses; other divisors (3, 0.5, -2) stay divisions. */
static bool pow2_divisor_p(tree d, REAL_VALUE_TYPE* inv) {
    d = tree_strip_any_location_wrapper(d);
    if ((TREE_CODE(d) == FLOAT_EXPR || TREE_CODE(d) == NOP_EXPR) &&
        TREE_CODE(tree_strip_any_location_wrapper(TREE_OPERAND(d, 0))) == INTEGER_CST) {
        /* x / 2: the integer converted, folded later */
        d = fold_build1(FLOAT_EXPR, TREE_TYPE(d), tree_strip_any_location_wrapper(TREE_OPERAND(d, 0)));
    }
    if (TREE_CODE(d) != REAL_CST) {
        return false;
    }
    REAL_VALUE_TYPE v = TREE_REAL_CST(d);
    if (!real_less(&dconst1, &v)) {
        return false;
    }
    REAL_VALUE_TYPE r = v;
    if (!exact_real_inverse(TYPE_MODE(TREE_TYPE(d)), &r)) {
        return false;
    }
    if (inv) {
        *inv = r;
    }
    return true;
}

/* GCC's folder of declaration initializers (cp_fully_fold_init, before the plugin sees
   them) rewrites -K + a*b as (T)(a*b) - K, a conversion to the same type. */
static tree strip_same_type(tree x) {
    while ((TREE_CODE(x) == NOP_EXPR || TREE_CODE(x) == CONVERT_EXPR) &&
           TYPE_MAIN_VARIANT(TREE_TYPE(x)) == TYPE_MAIN_VARIANT(TREE_TYPE(TREE_OPERAND(x, 0)))) {
        x = TREE_OPERAND(x, 0);
    }
    return x;
}

/* a multiply of this type; CodeWarrior folds x * 1.0 to x first */
static bool is_mult(tree x, tree type) {
    x = strip_same_type(x);
    if (TYPE_MAIN_VARIANT(TREE_TYPE(x)) != TYPE_MAIN_VARIANT(type)) {
        return false;
    }
    if (TREE_CODE(x) == RDIV_EXPR) {
        return pow2_divisor_p(TREE_OPERAND(x, 1), NULL);
    }
    return TREE_CODE(x) == MULT_EXPR && !real_cst_p(TREE_OPERAND(x, 0), dconst1) &&
           !real_cst_p(TREE_OPERAND(x, 1), dconst1);
}

enum kind { FMADD, FMSUB, FNMSUB };

static void report_site(location_t loc, kind k, tree type, bool negated) {
    static const char* names[] = {"fmadd", "fmsub", "fnmsub"};
    const char* what = names[k];
    if (negated) {
        what = k == FMADD ? "fnmadd" : "fnmsub";
    }
    expanded_location e = expand_location(ordinary(loc));
    expanded_location f = expand_location(ordinary(DECL_SOURCE_LOCATION(current_fn)));
    fprintf(report, "%s\t%s\t%s:%d:%d\t%s%s\t%s:%d\n", main_input_filename,
            IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(current_fn)), e.file ? e.file : "?", e.line,
            e.column, what, TYPE_MODE(type) == SFmode ? "s" : "", f.file ? f.file : "?", f.line);
}

/* t is a fusable add / subtract: the fused form, operands evaluated in the written order. */
static tree fuse(tree t, bool negated) {
    tree type = TREE_TYPE(t);
    location_t loc = EXPR_LOCATION(t);
    tree l = TREE_OPERAND(t, 0), r = TREE_OPERAND(t, 1);
    bool plus = TREE_CODE(t) == PLUS_EXPR;
    tree m, c;
    kind k;
    bool mult_first;
    if (is_mult(l, type)) {
        m = strip_same_type(l), c = r, k = plus ? FMADD : FMSUB, mult_first = true;
    } else {
        m = strip_same_type(r), c = l, k = plus ? FMADD : FNMSUB, mult_first = false;
    }
    tree a = TREE_OPERAND(m, 0), b = TREE_OPERAND(m, 1);
    if (TREE_CODE(m) == RDIV_EXPR) {
        REAL_VALUE_TYPE inv;
        pow2_divisor_p(b, &inv);
        b = build_real(type, inv);
    }
    tree seq = NULL_TREE;
    if (TREE_SIDE_EFFECTS(a) || TREE_SIDE_EFFECTS(b) || TREE_SIDE_EFFECTS(c)) {
        /* keep the order: a, b, c as written (c first when it is the left operand) */
        a = save_expr(a), b = save_expr(b), c = save_expr(c);
        tree order[3] = {a, b, c};
        if (!mult_first) {
            order[0] = c, order[1] = a, order[2] = b;
        }
        for (int i = 2; i >= 0; i--) {
            if (TREE_CODE(order[i]) == SAVE_EXPR) {
                seq = seq ? build2(COMPOUND_EXPR, void_type_node, order[i], seq) : order[i];
            }
        }
    }
    if (k != FMADD) {
        c = build1_loc(loc, NEGATE_EXPR, type, c);
        own_negations->add(c);
    }
    tree call = build_call_expr_loc(loc, fma_decl(type), 3, a, b, c);
    if (k == FNMSUB) {
        call = build1_loc(loc, NEGATE_EXPR, type, call);
    }
    if (seq) {
        call = build2_loc(loc, COMPOUND_EXPR, type, seq, call);
    }
    if (report) {
        report_site(loc, k, type, negated && k != FNMSUB);
    }
    return call;
}

static bool fusable(tree t) {
    if ((TREE_CODE(t) != PLUS_EXPR && TREE_CODE(t) != MINUS_EXPR) || !float_type_p(TREE_TYPE(t))) {
        return false;
    }
    tree type = TREE_TYPE(t);
    tree l = TREE_OPERAND(t, 0), r = TREE_OPERAND(t, 1);
    if (!is_mult(l, type) && !is_mult(r, type)) {
        return false;
    }
    /* CodeWarrior folds a*b + 0.0 to a*b first */
    if (TREE_CODE(t) == PLUS_EXPR && (real_cst_p(l, dconst0) || real_cst_p(r, dconst0))) {
        return false;
    }
    location_t loc = EXPR_LOCATION(t);
    if (loc == UNKNOWN_LOCATION) {
        loc = DECL_SOURCE_LOCATION(current_fn);
    }
    return contract_at(loc) && !excluded_at(loc);
}

/* x op= f() * y: C++17 evaluates the right side first, into a temporary,
   (T = f() * y, x = x + T); CodeWarrior fuses it as written. The operands are saved
   first, in their order, and the add takes the multiply again. */
static void compound_assignment(tree t) {
    tree te = TREE_OPERAND(t, 0);
    if (TREE_CODE(te) != TARGET_EXPR || TARGET_EXPR_CLEANUP(te)) {
        return;
    }
    /* the add, under the conversions of an integer or a narrower target (s32 x += ...) */
    tree e = TREE_OPERAND(t, 1);
    while (CONVERT_EXPR_P(e) || TREE_CODE(e) == FIX_TRUNC_EXPR || TREE_CODE(e) == NON_LVALUE_EXPR) {
        e = TREE_OPERAND(e, 0);
    }
    if ((TREE_CODE(e) != PLUS_EXPR && TREE_CODE(e) != MINUS_EXPR) || !float_type_p(TREE_TYPE(e))) {
        return;
    }
    location_t loc = EXPR_LOCATION(e) != UNKNOWN_LOCATION ? EXPR_LOCATION(e) : DECL_SOURCE_LOCATION(current_fn);
    if (!contract_at(loc) || excluded_at(loc)) {
        return;
    }
    tree slot = TARGET_EXPR_SLOT(te), init = TARGET_EXPR_INITIAL(te);
    int i = TREE_OPERAND(e, 1) == slot ? 1 : TREE_OPERAND(e, 0) == slot ? 0 : -1;
    if (i < 0 || !init || !is_mult(init, TREE_TYPE(e))) {
        return;
    }
    init = strip_same_type(init);
    tree a = save_expr(TREE_OPERAND(init, 0)), b = save_expr(TREE_OPERAND(init, 1));
    tree m = build2_loc(EXPR_LOCATION(init), TREE_CODE(init), TREE_TYPE(init), a, b);
    tree pre = build2(COMPOUND_EXPR, void_type_node, a, b);
    TREE_OPERAND(t, 0) = fold_convert(void_type_node, pre);
    TREE_OPERAND(e, i) = m;
}

static const char* intrinsic_kind(tree t) {
    tree fn = get_callee_fndecl(t);
    if (!fn || !DECL_NAME(fn)) {
        return NULL;
    }
    const char* n = IDENTIFIER_POINTER(DECL_NAME(fn));
    static const char* names[] = {"__fmadds", "__fmsubs", "__fnmadds", "__fnmsubs",
                                  "__fmadd",  "__fmsub",  "__fnmadd",  "__fnmsub"};
    for (const char* x : names) {
        if (!strcmp(n, x)) {
            return x + 2;
        }
    }
    return NULL;
}

static tree walk(tree* tp, int* walk_subtrees, void*) {
    tree t = *tp;
    if (TYPE_P(t)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    if (TREE_CODE(t) == COMPOUND_EXPR) {
        compound_assignment(t);
    }
    if (report && TREE_CODE(t) == CALL_EXPR) {
        /* the decomp's own __fmadds etc. (decomp/compat.h): fused as written; reported
           so that a comparison with the original counts them */
        const char* k = intrinsic_kind(t);
        if (k && !excluded_at(EXPR_LOCATION(t))) {
            expanded_location e = expand_location(ordinary(EXPR_LOCATION(t)));
            expanded_location f = expand_location(ordinary(DECL_SOURCE_LOCATION(current_fn)));
            fprintf(report, "%s\t%s\t%s:%d:%d\t%s\t%s:%d\texplicit\n", main_input_filename,
                    IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(current_fn)), e.file ? e.file : "?",
                    e.line, e.column, k, f.file ? f.file : "?", f.line);
        }
    }
    if (TREE_CODE(t) == NEGATE_EXPR && fusable(TREE_OPERAND(t, 0)) && !own_negations->contains(t)) {
        /* -(a*b + c): fnmadd, -(a*b - c): fnmsub; the negation stays, exact */
        TREE_OPERAND(t, 0) = fuse(TREE_OPERAND(t, 0), true);
    } else if (fusable(t)) {
        *tp = fuse(t, false);
    } else if (TREE_CODE(t) == NEGATE_EXPR && own_negations->contains(t) && fusable(TREE_OPERAND(t, 0))) {
        TREE_OPERAND(t, 0) = fuse(TREE_OPERAND(t, 0), false);
    }
    return NULL_TREE;
}

static void pre_genericize(void* gcc_data, void*) {
    tree fn = (tree)gcc_data;
    if (!fn || TREE_CODE(fn) != FUNCTION_DECL || !DECL_SAVED_TREE(fn)) {
        return;
    }
    current_fn = fn;
    if (getenv("CWCONTRACT_DEBUG")) {
        print_generic_stmt(stderr, DECL_SAVED_TREE(fn), TDF_NONE);
    }
    hash_set<tree> visited, negs;
    own_negations = &negs;
    walk_tree(&DECL_SAVED_TREE(fn), walk, NULL, &visited);
    own_negations = NULL;
    current_fn = NULL_TREE;
}

static void finish(void*, void*) {
    if (report) {
        fclose(report);
    }
}

int plugin_init(struct plugin_name_args* info, struct plugin_gcc_version* version) {
    if (!plugin_default_version_check(version, &gcc_version)) {
        error("cwcontract: built for GCC %s, loaded by %s", gcc_version.basever, version->basever);
        return 1;
    }
    for (int i = 0; i < info->argc; i++) {
        const char* k = info->argv[i].key;
        const char* v = info->argv[i].value;
        if (!strcmp(k, "default") && v) {
            contract_default = !strcmp(v, "on");
        } else if (!strcmp(k, "exclude") && v) {
            /* a colon-separated list of directories */
            for (const char* p = v; *p;) {
                const char* e = strchr(p, ':');
                size_t n = e ? (size_t)(e - p) : strlen(p);
                if (n) {
                    excluded.emplace_back(p, n);
                }
                p += n + (e ? 1 : 0);
            }
        } else if (!strcmp(k, "report") && v) {
            report = fopen(v, "a");
        }
    }
    register_callback(info->base_name, PLUGIN_PRAGMAS, register_pragmas, NULL);
    register_callback(info->base_name, PLUGIN_PRE_GENERICIZE, pre_genericize, NULL);
    register_callback(info->base_name, PLUGIN_FINISH, finish, NULL);
    return 0;
}
