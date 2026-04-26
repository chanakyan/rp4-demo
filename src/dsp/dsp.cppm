// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// dsp.cppm — runtime Faust interpreter as state monad
//
// Architecture:
//   Parse .dsp → AST → eval(AST, Env) → Signal graph → tick() per sample
//
//   Parse time: resolve names, substitute function arguments,
//               constant-fold arithmetic, size delay lines, allocate State.
//   Tick time:  walk the graph, read/write State. Zero allocation.
//               Intermediates live on the C++ call stack.
//
// The monad:
//   Signal::tick(State&, span<const float> in, span<float> out) → void
//   in/out are caller-provided spans. No heap allocation per sample.
//
// Faust operators as monad combinators:
//   :   Seq     bind         A's out feeds B's in
//   ,   Par     product      split inputs, concat outputs
//   <:  Split   fanout       A's out replicated to B
//   :>  Merge   fold         sum A's outs down to B's width
//   ~   Rec     fixpoint     B's out feeds back into A
//
// Two representations:
//   Expr  — parsed AST, not yet evaluated (S-expression)
//   Node  — evaluated signal graph, ready to tick (closure)
//
// eval() is the metacircular evaluator. Expr × Env → Node.
// Functions are first-class: allpass(dt,fb) captures args in closure.

export module dsp;
import std;

export namespace dsp {

// ═══════════════════════════════════════════════════════════════
// §1  STATE — delay lines + feedback + params. Allocated once.
// ═══════════════════════════════════════════════════════════════

inline int g_next_id = 0;
inline auto fresh_id() -> int { return g_next_id++; }

struct State {
    std::unordered_map<int, std::vector<float>> delays;
    std::unordered_map<int, int>                delay_pos;
    std::unordered_map<int, float>              feedback;
    std::unordered_map<std::string, float>      params;
    float sr = 48000.f;

    auto delay_write_read(int id, int len, float x, int d) -> float {
        auto& buf = delays[id];
        if (buf.empty()) { buf.resize(len, 0.f); delay_pos[id] = 0; }
        auto& wp = delay_pos[id];
        buf[wp] = x;
        float out = buf[(wp - std::clamp(d, 0, len-1) + len) % len];
        wp = (wp + 1) % len;
        return out;
    }

    auto fb_get(int id) -> float {
        auto it = feedback.find(id);
        return it != feedback.end() ? it->second : 0.f;
    }
    auto fb_set(int id, float v) -> void { feedback[id] = v; }

    auto param_get(const std::string& label, float init) -> float {
        auto it = params.find(label);
        return it != params.end() ? it->second : init;
    }
    auto param_set(const std::string& label, float v) -> void {
        params[label] = v;
    }
};

// ═══════════════════════════════════════════════════════════════
// §2  SIGNAL — the monad. tick() writes to caller-provided span.
// ═══════════════════════════════════════════════════════════════

struct Signal {
    virtual ~Signal() = default;
    virtual auto tick(State& s, std::span<const float> in,
                      std::span<float> out) -> void = 0;
    virtual auto n_in()  const -> int = 0;
    virtual auto n_out() const -> int = 0;
};

using Node = std::unique_ptr<Signal>;

// ═══════════════════════════════════════════════════════════════
// §3  PRIMITIVES — leaf nodes
// ═══════════════════════════════════════════════════════════════

struct Constant final : Signal {
    float val;
    Constant(float v) : val(v) {}
    auto tick(State&, std::span<const float>, std::span<float> out) -> void override {
        out[0] = val;
    }
    auto n_in()  const -> int override { return 0; }
    auto n_out() const -> int override { return 1; }
};

struct Wire final : Signal {
    auto tick(State&, std::span<const float> in, std::span<float> out) -> void override {
        out[0] = in.empty() ? 0.f : in[0];
    }
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 1; }
};

struct Cut final : Signal {
    auto tick(State&, std::span<const float>, std::span<float>) -> void override {}
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 0; }
};

struct BinOp final : Signal {
    enum Op { Add,Sub,Mul,Div,Mod,Pow,Min,Max,
              And,Or,Xor,Shl,Shr,Atan2,Rem };
    Op op;
    BinOp(Op o) : op(o) {}
    auto tick(State&, std::span<const float> in, std::span<float> out) -> void override {
        float a = in.size() > 0 ? in[0] : 0.f;
        float b = in.size() > 1 ? in[1] : 0.f;
        switch (op) {
        case Add: out[0] = a + b; break;
        case Sub: out[0] = a - b; break;
        case Mul: out[0] = a * b; break;
        case Div: out[0] = b != 0.f ? a / b : 0.f; break;
        case Mod: out[0] = b != 0.f ? std::fmod(a,b) : 0.f; break;
        case Pow: out[0] = std::pow(a,b); break;
        case Min: out[0] = std::min(a,b); break;
        case Max: out[0] = std::max(a,b); break;
        case And: out[0] = float(int(a) & int(b)); break;
        case Or:  out[0] = float(int(a) | int(b)); break;
        case Xor: out[0] = float(int(a) ^ int(b)); break;
        case Shl: out[0] = float(int(a) << int(b)); break;
        case Shr: out[0] = float(int(a) >> int(b)); break;
        case Atan2: out[0] = std::atan2(a,b); break;
        case Rem: out[0] = b != 0.f ? std::remainder(a,b) : 0.f; break;
        }
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

struct UnaryOp final : Signal {
    enum Op { Sin,Cos,Tan,Asin,Acos,Atan,Sqrt,Exp,
              Log,Log10,Abs,Floor,Ceil,Rint,ToInt,ToFloat,Neg };
    Op op;
    UnaryOp(Op o) : op(o) {}
    auto tick(State&, std::span<const float> in, std::span<float> out) -> void override {
        float a = in.empty() ? 0.f : in[0];
        switch (op) {
        case Sin:   out[0] = std::sin(a); break;
        case Cos:   out[0] = std::cos(a); break;
        case Tan:   out[0] = std::tan(a); break;
        case Asin:  out[0] = std::asin(a); break;
        case Acos:  out[0] = std::acos(a); break;
        case Atan:  out[0] = std::atan(a); break;
        case Sqrt:  out[0] = std::sqrt(std::max(0.f,a)); break;
        case Exp:   out[0] = std::exp(a); break;
        case Log:   out[0] = std::log(std::max(1e-30f,a)); break;
        case Log10: out[0] = std::log10(std::max(1e-30f,a)); break;
        case Abs:   out[0] = std::abs(a); break;
        case Floor: out[0] = std::floor(a); break;
        case Ceil:  out[0] = std::ceil(a); break;
        case Rint:  out[0] = std::rint(a); break;
        case ToInt: out[0] = float(int(a)); break;
        case ToFloat: out[0] = a; break;
        case Neg:   out[0] = -a; break;
        }
    }
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 1; }
};

struct Select2 final : Signal {
    auto tick(State&, std::span<const float> in, std::span<float> out) -> void override {
        float sel = in.size() > 0 ? in[0] : 0.f;
        float a   = in.size() > 1 ? in[1] : 0.f;
        float b   = in.size() > 2 ? in[2] : 0.f;
        out[0] = sel != 0.f ? b : a;
    }
    auto n_in()  const -> int override { return 3; }
    auto n_out() const -> int override { return 1; }
};

// ═══════════════════════════════════════════════════════════════
// §4  STATE NODES — mem, delay, tables
// ═══════════════════════════════════════════════════════════════

struct MemNode final : Signal {
    int id;
    MemNode() : id(fresh_id()) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        out[0] = s.fb_get(id);
        s.fb_set(id, in.empty() ? 0.f : in[0]);
    }
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 1; }
};

struct DelayNode final : Signal {
    int id, max_len;
    DelayNode(int n) : id(fresh_id()), max_len(std::max(1,n)) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        float x = in.size() > 0 ? in[0] : 0.f;
        int d   = in.size() > 1 ? int(in[1]) : 0;
        out[0]  = s.delay_write_read(id, max_len, x, d);
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// Fixed delay: @(N) where N is known at parse time
struct FixedDelay final : Signal {
    int id, len;
    FixedDelay(int n) : id(fresh_id()), len(std::max(1,n)) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        out[0] = s.delay_write_read(id, len, in.empty() ? 0.f : in[0], len);
    }
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 1; }
};

struct RdTable final : Signal {
    std::vector<float> table; // allocated once at parse time
    RdTable(std::vector<float> t) : table(std::move(t)) {}
    auto tick(State&, std::span<const float> in, std::span<float> out) -> void override {
        int idx = in.empty() ? 0 : std::clamp(int(in[0]), 0, int(table.size())-1);
        out[0] = table[idx];
    }
    auto n_in()  const -> int override { return 1; }
    auto n_out() const -> int override { return 1; }
};

struct RwTable final : Signal {
    int id, size;
    RwTable(int n) : id(fresh_id()), size(std::max(1,n)) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        int widx  = in.size() > 0 ? std::clamp(int(in[0]), 0, size-1) : 0;
        float wv  = in.size() > 1 ? in[1] : 0.f;
        int ridx  = in.size() > 2 ? std::clamp(int(in[2]), 0, size-1) : 0;
        auto& buf = s.delays[id];
        if (buf.empty()) buf.resize(size, 0.f);
        buf[widx] = wv;
        out[0] = buf[ridx];
    }
    auto n_in()  const -> int override { return 3; }
    auto n_out() const -> int override { return 1; }
};

// ═══════════════════════════════════════════════════════════════
// §5  UI — sliders, buttons, nentry, checkbox
// ═══════════════════════════════════════════════════════════════

struct SliderNode final : Signal {
    std::string label; float init, lo, hi;
    SliderNode(std::string l, float i, float lo_, float hi_)
        : label(std::move(l)), init(i), lo(lo_), hi(hi_) {}
    auto tick(State& s, std::span<const float>, std::span<float> out) -> void override {
        out[0] = std::clamp(s.param_get(label, init), lo, hi);
    }
    auto n_in()  const -> int override { return 0; }
    auto n_out() const -> int override { return 1; }
};

struct ButtonNode final : Signal {
    std::string label;
    ButtonNode(std::string l) : label(std::move(l)) {}
    auto tick(State& s, std::span<const float>, std::span<float> out) -> void override {
        out[0] = s.param_get(label, 0.f);
    }
    auto n_in()  const -> int override { return 0; }
    auto n_out() const -> int override { return 1; }
};

struct CheckboxNode final : Signal {
    std::string label;
    CheckboxNode(std::string l) : label(std::move(l)) {}
    auto tick(State& s, std::span<const float>, std::span<float> out) -> void override {
        out[0] = s.param_get(label, 0.f) != 0.f ? 1.f : 0.f;
    }
    auto n_in()  const -> int override { return 0; }
    auto n_out() const -> int override { return 1; }
};

struct NentryNode final : Signal {
    std::string label; float init, lo, hi;
    NentryNode(std::string l, float i, float lo_, float hi_)
        : label(std::move(l)), init(i), lo(lo_), hi(hi_) {}
    auto tick(State& s, std::span<const float>, std::span<float> out) -> void override {
        out[0] = std::clamp(s.param_get(label, init), lo, hi);
    }
    auto n_in()  const -> int override { return 0; }
    auto n_out() const -> int override { return 1; }
};

// ═══════════════════════════════════════════════════════════════
// §6  COMBINATORS — the monad operations
//     Each owns its scratch buffer (allocated at build time).
// ═══════════════════════════════════════════════════════════════

struct SeqNode final : Signal {
    Node a, b;
    std::vector<float> scratch; // a.n_out() floats, allocated once
    SeqNode(Node a_, Node b_)
        : a(std::move(a_)), b(std::move(b_))
        , scratch(a->n_out(), 0.f) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        a->tick(s, in, scratch);
        b->tick(s, scratch, out);
    }
    auto n_in()  const -> int override { return a->n_in(); }
    auto n_out() const -> int override { return b->n_out(); }
};

struct ParNode final : Signal {
    Node a, b;
    int ai, ao, bi, bo;
    std::vector<float> a_out, b_out; // scratch for each side
    ParNode(Node a_, Node b_)
        : a(std::move(a_)), b(std::move(b_))
        , ai(a->n_in()), ao(a->n_out()), bi(b->n_in()), bo(b->n_out())
        , a_out(ao, 0.f), b_out(bo, 0.f) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        auto a_in = in.subspan(0, std::min(ai, int(in.size())));
        auto b_in = int(in.size()) > ai ? in.subspan(ai) : std::span<const float>{};
        a->tick(s, a_in, a_out);
        b->tick(s, b_in, b_out);
        // concat into out
        for (int i = 0; i < ao; ++i) out[i] = a_out[i];
        for (int i = 0; i < bo; ++i) out[ao+i] = b_out[i];
    }
    auto n_in()  const -> int override { return ai + bi; }
    auto n_out() const -> int override { return ao + bo; }
};

struct SplitNode final : Signal {
    Node a, b;
    int ao;
    std::vector<float> a_out, expanded;
    SplitNode(Node a_, Node b_)
        : a(std::move(a_)), b(std::move(b_))
        , ao(a->n_out()), a_out(ao, 0.f)
        , expanded(b->n_in(), 0.f) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        a->tick(s, in, a_out);
        int bi = b->n_in();
        for (int i = 0; i < bi; ++i) expanded[i] = a_out[i % ao];
        b->tick(s, expanded, out);
    }
    auto n_in()  const -> int override { return a->n_in(); }
    auto n_out() const -> int override { return b->n_out(); }
};

struct MergeNode final : Signal {
    Node a, b;
    int ao, bi;
    std::vector<float> a_out, summed;
    MergeNode(Node a_, Node b_)
        : a(std::move(a_)), b(std::move(b_))
        , ao(a->n_out()), bi(b->n_in())
        , a_out(ao, 0.f), summed(bi, 0.f) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        a->tick(s, in, a_out);
        for (int i = 0; i < bi; ++i) summed[i] = 0.f;
        for (int i = 0; i < ao; ++i) summed[i % bi] += a_out[i];
        b->tick(s, summed, out);
    }
    auto n_in()  const -> int override { return a->n_in(); }
    auto n_out() const -> int override { return b->n_out(); }
};

struct RecNode final : Signal {
    Node a, b;
    int fb_outs, ext_in;
    int fb_id;
    std::vector<float> ext, a_out, b_out;
    RecNode(Node a_, Node b_)
        : a(std::move(a_)), b(std::move(b_))
        , fb_outs(b->n_out())
        , ext_in(a->n_in())
        , fb_id(fresh_id())
        , ext(ext_in, 0.f)
        , a_out(a->n_out(), 0.f)
        , b_out(fb_outs, 0.f) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        // build extended input: external inputs + feedback
        int real_in = ext_in - fb_outs;
        for (int i = 0; i < real_in && i < int(in.size()); ++i)
            ext[i] = in[i];
        for (int i = 0; i < fb_outs; ++i)
            ext[real_in + i] = s.fb_get(fb_id + i);
        a->tick(s, ext, a_out);
        b->tick(s, a_out, b_out);
        for (int i = 0; i < fb_outs; ++i)
            s.fb_set(fb_id + i, b_out[i]);
        // output is a's output
        for (int i = 0; i < a->n_out() && i < int(out.size()); ++i)
            out[i] = a_out[i];
    }
    auto n_in()  const -> int override { return std::max(0, ext_in - fb_outs); }
    auto n_out() const -> int override { return a->n_out(); }
};

struct GroupNode final : Signal {
    Node body;
    GroupNode(Node b) : body(std::move(b)) {}
    auto tick(State& s, std::span<const float> in, std::span<float> out) -> void override {
        body->tick(s, in, out);
    }
    auto n_in()  const -> int override { return body->n_in(); }
    auto n_out() const -> int override { return body->n_out(); }
};

// ═══════════════════════════════════════════════════════════════
// §7  BUILDERS
// ═══════════════════════════════════════════════════════════════

inline auto mk_const(float v) -> Node { return std::make_unique<Constant>(v); }
inline auto mk_wire()         -> Node { return std::make_unique<Wire>(); }
inline auto mk_cut()          -> Node { return std::make_unique<Cut>(); }
inline auto mk_mem()          -> Node { return std::make_unique<MemNode>(); }
inline auto mk_delay(int n)   -> Node { return std::make_unique<FixedDelay>(n); }
inline auto mk_vdelay()       -> Node { return std::make_unique<DelayNode>(192000); }
inline auto mk_select2()      -> Node { return std::make_unique<Select2>(); }

inline auto mk_binop(BinOp::Op o) -> Node { return std::make_unique<BinOp>(o); }
inline auto mk_unary(UnaryOp::Op o) -> Node { return std::make_unique<UnaryOp>(o); }

inline auto mk_slider(std::string l, float i, float lo, float hi) -> Node {
    return std::make_unique<SliderNode>(std::move(l), i, lo, hi);
}
inline auto mk_button(std::string l) -> Node {
    return std::make_unique<ButtonNode>(std::move(l));
}
inline auto mk_checkbox(std::string l) -> Node {
    return std::make_unique<CheckboxNode>(std::move(l));
}
inline auto mk_nentry(std::string l, float i, float lo, float hi) -> Node {
    return std::make_unique<NentryNode>(std::move(l), i, lo, hi);
}

inline auto mk_seq(Node a, Node b) -> Node {
    return std::make_unique<SeqNode>(std::move(a), std::move(b));
}
inline auto mk_par(Node a, Node b) -> Node {
    return std::make_unique<ParNode>(std::move(a), std::move(b));
}
inline auto mk_split(Node a, Node b) -> Node {
    return std::make_unique<SplitNode>(std::move(a), std::move(b));
}
inline auto mk_merge(Node a, Node b) -> Node {
    return std::make_unique<MergeNode>(std::move(a), std::move(b));
}
inline auto mk_rec(Node a, Node b) -> Node {
    return std::make_unique<RecNode>(std::move(a), std::move(b));
}
inline auto mk_group(Node b) -> Node {
    return std::make_unique<GroupNode>(std::move(b));
}

// ═══════════════════════════════════════════════════════════════
// §8  AST — parsed but not yet evaluated
//     This is the S-expression. eval() turns it into Nodes.
// ═══════════════════════════════════════════════════════════════

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    enum Kind {
        ENum, EWire, ECut,
        EBinOp, EUnaryOp, ESelect2,
        EMem, EDelay, EFixedDelay,
        ESlider, EButton, ECheckbox, ENentry,
        ESeq, EPar, ESplit, EMerge, ERec,
        EIdent, ECall, EWith, EGroup,
        ERdTable, ERwTable, EPrefix
    };
    Kind kind;
    float num = 0.f;
    std::string name;    // ident, label, op name
    std::string name2;   // group kind (hgroup/vgroup/tgroup), slider kind
    ExprPtr left, right; // binary combinators
    std::vector<ExprPtr> args; // function call args
    std::vector<std::pair<std::string, ExprPtr>> defs; // with block
};

inline auto e_num(float v) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ENum; e->num = v; return e;
}
inline auto e_wire() -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EWire; return e;
}
inline auto e_cut() -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ECut; return e;
}
inline auto e_ident(std::string n) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EIdent; e->name = std::move(n); return e;
}
inline auto e_binop(std::string op) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EBinOp; e->name = std::move(op); return e;
}
inline auto e_unary(std::string op) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EUnaryOp; e->name = std::move(op); return e;
}
inline auto e_mem() -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EMem; return e;
}
inline auto e_delay() -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EDelay; return e;
}
inline auto e_seq(ExprPtr a, ExprPtr b) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ESeq;
    e->left = std::move(a); e->right = std::move(b); return e;
}
inline auto e_par(ExprPtr a, ExprPtr b) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EPar;
    e->left = std::move(a); e->right = std::move(b); return e;
}
inline auto e_split(ExprPtr a, ExprPtr b) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ESplit;
    e->left = std::move(a); e->right = std::move(b); return e;
}
inline auto e_merge(ExprPtr a, ExprPtr b) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EMerge;
    e->left = std::move(a); e->right = std::move(b); return e;
}
inline auto e_rec(ExprPtr a, ExprPtr b) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ERec;
    e->left = std::move(a); e->right = std::move(b); return e;
}
inline auto e_call(std::string fn, std::vector<ExprPtr> args) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ECall;
    e->name = std::move(fn); e->args = std::move(args); return e;
}
inline auto e_slider(std::string kind, std::string label, float init, float lo, float hi, float step) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ESlider;
    e->name2 = std::move(kind); e->name = std::move(label);
    e->num = init;
    e->args = {e_num(lo), e_num(hi), e_num(step)};
    return e;
}
inline auto e_button(std::string label) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EButton;
    e->name = std::move(label); return e;
}
inline auto e_checkbox(std::string label) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ECheckbox;
    e->name = std::move(label); return e;
}
inline auto e_nentry(std::string label, float init, float lo, float hi, float step) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::ENentry;
    e->name = std::move(label); e->num = init;
    e->args = {e_num(lo), e_num(hi), e_num(step)};
    return e;
}
inline auto e_group(std::string kind, std::string label, ExprPtr body) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EGroup;
    e->name2 = std::move(kind); e->name = std::move(label);
    e->left = std::move(body); return e;
}
inline auto e_with(ExprPtr body, std::vector<std::pair<std::string,ExprPtr>> defs) -> ExprPtr {
    auto e = std::make_shared<Expr>(); e->kind = Expr::EWith;
    e->left = std::move(body); e->defs = std::move(defs); return e;
}

// ═══════════════════════════════════════════════════════════════
// §9  ENVIRONMENT — name bindings for eval
// ═══════════════════════════════════════════════════════════════

struct FuncDef {
    std::vector<std::string> params;
    ExprPtr body;
};

struct Env {
    std::unordered_map<std::string, ExprPtr> vals;    // name = expr
    std::unordered_map<std::string, FuncDef> funcs;   // name(args) = expr
    std::shared_ptr<Env> parent;                       // lexical scope

    auto lookup_val(const std::string& n) const -> ExprPtr {
        auto it = vals.find(n);
        if (it != vals.end()) return it->second;
        if (parent) return parent->lookup_val(n);
        return nullptr;
    }
    auto lookup_func(const std::string& n) const -> const FuncDef* {
        auto it = funcs.find(n);
        if (it != funcs.end()) return &it->second;
        if (parent) return parent->lookup_func(n);
        return nullptr;
    }
    auto child() -> std::shared_ptr<Env> {
        auto c = std::make_shared<Env>();
        c->parent = std::shared_ptr<Env>(this, [](auto*){});
        return c;
    }

    using NativeFunc = std::function<Node(const std::vector<ExprPtr>&, Env&)>;
    std::unordered_map<std::string, NativeFunc> natives;

    auto lookup_native(const std::string& n) const -> const NativeFunc* {
        auto it = natives.find(n);
        if (it != natives.end()) return &it->second;
        if (parent) return parent->lookup_native(n);
        return nullptr;
    }
};

// ═══════════════════════════════════════════════════════════════
// §10  CONSTANT FOLDING — evaluate pure arithmetic at parse time
//      Returns nullopt if the expression is signal-dependent.
// ═══════════════════════════════════════════════════════════════

inline auto try_const_eval(const ExprPtr& e, const Env& env) -> std::optional<float> {
    if (!e) return std::nullopt;
    switch (e->kind) {
    case Expr::ENum: return e->num;
    case Expr::EIdent: {
        auto v = env.lookup_val(e->name);
        if (v) return try_const_eval(v, env);
        return std::nullopt;
    }
    case Expr::ESeq: {
        // A : B where both are constant
        auto a = try_const_eval(e->left, env);
        if (!a) return std::nullopt;
        // B applied to a — only works for unary/binary ops
        if (e->right && e->right->kind == Expr::EBinOp) {
            // need second operand from A, but A is single const
            return std::nullopt;
        }
        if (e->right && e->right->kind == Expr::EUnaryOp) {
            auto& op = e->right->name;
            if (op == "neg") return -*a;
            if (op == "int") return float(int(*a));
            if (op == "float") return *a;
            if (op == "sin") return std::sin(*a);
            if (op == "cos") return std::cos(*a);
            // ... can extend
        }
        return std::nullopt;
    }
    case Expr::EPar: {
        // A , B : op  — check if parent is Seq with binop
        return std::nullopt; // can't fold par alone
    }
    case Expr::ECall: {
        // try to resolve function and fold
        auto fd = env.lookup_func(e->name);
        if (!fd) return std::nullopt;
        auto child_env = std::make_shared<Env>();
        child_env->parent = std::shared_ptr<Env>(const_cast<Env*>(&env), [](auto*){});
        for (size_t i = 0; i < fd->params.size() && i < e->args.size(); ++i) {
            auto cv = try_const_eval(e->args[i], env);
            if (!cv) return std::nullopt;
            child_env->vals[fd->params[i]] = e_num(*cv);
        }
        return try_const_eval(fd->body, *child_env);
    }
    default: return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════════
// §11  EVAL — the metacircular evaluator. Expr × Env → Node.
//      This is where Lisp happens.
// ═══════════════════════════════════════════════════════════════

inline auto eval(const ExprPtr& e, Env& env) -> Node;

inline auto eval(const ExprPtr& e, Env& env) -> Node {
    if (!e) return mk_wire();

    // try constant fold first
    auto cv = try_const_eval(e, env);
    if (cv) return mk_const(*cv);

    switch (e->kind) {
    case Expr::ENum:    return mk_const(e->num);
    case Expr::EWire:   return mk_wire();
    case Expr::ECut:    return mk_cut();
    case Expr::EMem:    return mk_mem();
    case Expr::EDelay:  return mk_vdelay();

    case Expr::EBinOp: {
        auto& op = e->name;
        if (op == "+") return mk_binop(BinOp::Add);
        if (op == "-") return mk_binop(BinOp::Sub);
        if (op == "*") return mk_binop(BinOp::Mul);
        if (op == "/") return mk_binop(BinOp::Div);
        if (op == "%") return mk_binop(BinOp::Mod);
        if (op == "^") return mk_binop(BinOp::Pow);
        if (op == "min") return mk_binop(BinOp::Min);
        if (op == "max") return mk_binop(BinOp::Max);
        if (op == "&") return mk_binop(BinOp::And);
        if (op == "|") return mk_binop(BinOp::Or);
        if (op == "xor") return mk_binop(BinOp::Xor);
        if (op == "<<") return mk_binop(BinOp::Shl);
        if (op == ">>") return mk_binop(BinOp::Shr);
        if (op == "atan2") return mk_binop(BinOp::Atan2);
        if (op == "remainder") return mk_binop(BinOp::Rem);
        return mk_wire();
    }
    case Expr::EUnaryOp: {
        auto& op = e->name;
        if (op == "sin")   return mk_unary(UnaryOp::Sin);
        if (op == "cos")   return mk_unary(UnaryOp::Cos);
        if (op == "tan")   return mk_unary(UnaryOp::Tan);
        if (op == "asin")  return mk_unary(UnaryOp::Asin);
        if (op == "acos")  return mk_unary(UnaryOp::Acos);
        if (op == "atan")  return mk_unary(UnaryOp::Atan);
        if (op == "sqrt")  return mk_unary(UnaryOp::Sqrt);
        if (op == "exp")   return mk_unary(UnaryOp::Exp);
        if (op == "log")   return mk_unary(UnaryOp::Log);
        if (op == "log10") return mk_unary(UnaryOp::Log10);
        if (op == "abs")   return mk_unary(UnaryOp::Abs);
        if (op == "floor") return mk_unary(UnaryOp::Floor);
        if (op == "ceil")  return mk_unary(UnaryOp::Ceil);
        if (op == "rint")  return mk_unary(UnaryOp::Rint);
        if (op == "int")   return mk_unary(UnaryOp::ToInt);
        if (op == "float") return mk_unary(UnaryOp::ToFloat);
        if (op == "neg")   return mk_unary(UnaryOp::Neg);
        return mk_wire();
    }
    case Expr::ESelect2: return mk_select2();

    case Expr::ESlider:
        return mk_slider(e->name, e->num,
            e->args.size()>0 ? e->args[0]->num : 0.f,
            e->args.size()>1 ? e->args[1]->num : 1.f);
    case Expr::EButton:   return mk_button(e->name);
    case Expr::ECheckbox: return mk_checkbox(e->name);
    case Expr::ENentry:
        return mk_nentry(e->name, e->num,
            e->args.size()>0 ? e->args[0]->num : 0.f,
            e->args.size()>1 ? e->args[1]->num : 1.f);

    case Expr::ESeq:   return mk_seq(eval(e->left, env), eval(e->right, env));
    case Expr::EPar:   return mk_par(eval(e->left, env), eval(e->right, env));
    case Expr::ESplit:  return mk_split(eval(e->left, env), eval(e->right, env));
    case Expr::EMerge:  return mk_merge(eval(e->left, env), eval(e->right, env));
    case Expr::ERec:    return mk_rec(eval(e->left, env), eval(e->right, env));
    case Expr::EGroup:  return mk_group(eval(e->left, env));

    case Expr::EFixedDelay: {
        auto cv2 = try_const_eval(e->left, env);
        int len = cv2 ? int(*cv2) : 4096;
        return mk_delay(len);
    }

    case Expr::EIdent: {
        // look up in environment
        auto v = env.lookup_val(e->name);
        if (v) return eval(v, env);
        // might be a zero-arg function
        auto fd = env.lookup_func(e->name);
        if (fd && fd->params.empty()) return eval(fd->body, env);
        // unresolved — return wire as fallback
        return mk_wire();
    }

    case Expr::ECall: {
        auto nf = env.lookup_native(e->name);
        if (nf) return (*nf)(e->args, env);
        auto fd = env.lookup_func(e->name);
        if (!fd) return mk_wire(); // unresolved

        // create child scope, bind arguments
        Env child;
        child.parent = std::shared_ptr<Env>(&env, [](auto*){});
        // copy function defs so recursive calls work
        child.funcs = env.funcs;
        for (size_t i = 0; i < fd->params.size() && i < e->args.size(); ++i) {
            child.vals[fd->params[i]] = e->args[i];
        }
        return eval(fd->body, child);
    }

    case Expr::EWith: {
        Env child;
        child.parent = std::shared_ptr<Env>(&env, [](auto*){});
        child.funcs = env.funcs;
        for (auto& [name, def] : e->defs) {
            child.vals[name] = def;
        }
        return eval(e->left, child);
    }

    default: return mk_wire();
    }
}

// ═══════════════════════════════════════════════════════════════
// §12  TOKENIZER
// ═══════════════════════════════════════════════════════════════

enum class Tok {
    Num, Id, Str,
    Colon, Comma, Semi, Eq, LP, RP, LB, RB,
    SplitOp, MergeOp, Tilde, Under, Bang, At,
    Plus, Minus, Star, Slash, Percent, Caret,
    Amp, Pipe, LShift, RShift, Dot,
    Eof
};

struct Token { Tok type; std::string text; float num = 0.f; };

inline auto tokenize(std::string_view src) -> std::vector<Token> {
    std::vector<Token> ts;
    size_t i = 0;
    auto pk = [&]() -> char { return i < src.size() ? src[i] : '\0'; };
    auto adv = [&]() { ++i; };

    while (i < src.size()) {
        char c = pk();
        if (std::isspace(c)) { adv(); continue; }
        if (c == '/' && i+1 < src.size() && src[i+1] == '/') {
            while (i < src.size() && src[i] != '\n') adv();
            continue;
        }
        if (c == '/' && i+1 < src.size() && src[i+1] == '*') {
            i += 2;
            while (i+1 < src.size() && !(src[i]=='*' && src[i+1]=='/')) adv();
            if (i+1 < src.size()) i += 2;
            continue;
        }
        if (c=='(') { adv(); ts.push_back({Tok::LP}); continue; }
        if (c==')') { adv(); ts.push_back({Tok::RP}); continue; }
        if (c=='{') { adv(); ts.push_back({Tok::LB}); continue; }
        if (c=='}') { adv(); ts.push_back({Tok::RB}); continue; }
        if (c==';') { adv(); ts.push_back({Tok::Semi}); continue; }
        if (c=='=') { adv(); ts.push_back({Tok::Eq}); continue; }
        if (c==',') { adv(); ts.push_back({Tok::Comma}); continue; }
        if (c=='~') { adv(); ts.push_back({Tok::Tilde}); continue; }
        if (c=='_') { adv(); ts.push_back({Tok::Under}); continue; }
        if (c=='!') { adv(); ts.push_back({Tok::Bang}); continue; }
        if (c=='@') { adv(); ts.push_back({Tok::At}); continue; }
        if (c=='+') {
            adv();
            if (pk()=='>') { adv(); ts.push_back({Tok::MergeOp, "+>"}); }
            else ts.push_back({Tok::Plus});
            continue;
        }
        if (c=='-') { adv(); ts.push_back({Tok::Minus}); continue; }
        if (c=='*') { adv(); ts.push_back({Tok::Star}); continue; }
        if (c=='%') { adv(); ts.push_back({Tok::Percent}); continue; }
        if (c=='^') { adv(); ts.push_back({Tok::Caret}); continue; }
        if (c=='&') { adv(); ts.push_back({Tok::Amp}); continue; }
        if (c=='|') { adv(); ts.push_back({Tok::Pipe}); continue; }
        if (c=='.') { adv(); ts.push_back({Tok::Dot}); continue; }
        if (c==':') {
            adv();
            if (pk()=='>') { adv(); ts.push_back({Tok::MergeOp, ":>"}); }
            else ts.push_back({Tok::Colon});
            continue;
        }
        if (c=='<') {
            adv();
            if (pk()==':') { adv(); ts.push_back({Tok::SplitOp}); }
            else if (pk()=='<') { adv(); ts.push_back({Tok::LShift}); }
            continue;
        }
        if (c=='>') {
            adv();
            if (pk()=='>') { adv(); ts.push_back({Tok::RShift}); }
            continue;
        }
        if (c=='/') { adv(); ts.push_back({Tok::Slash}); continue; }
        if (c=='"') {
            adv(); std::string s;
            while (i < src.size() && pk()!='"') { s += pk(); adv(); }
            if (i < src.size()) adv();
            ts.push_back({Tok::Str, s});
            continue;
        }
        if (std::isdigit(c) || (c=='.' && i+1<src.size() && std::isdigit(src[i+1]))) {
            size_t st = i;
            while (i < src.size() && (std::isdigit(pk())||pk()=='.'||pk()=='e'||pk()=='E'))
                adv();
            auto ns = std::string(src.substr(st, i-st));
            ts.push_back({Tok::Num, ns, std::stof(ns)});
            continue;
        }
        if (std::isalpha(c) || c=='_') {
            size_t st = i;
            while (i < src.size() && (std::isalnum(pk())||pk()=='_'||pk()=='\''))
                adv();
            ts.push_back({Tok::Id, std::string(src.substr(st, i-st))});
            continue;
        }
        adv();
    }
    ts.push_back({Tok::Eof});
    return ts;
}

// ═══════════════════════════════════════════════════════════════
// §13  PARSER — recursive descent, builds AST
// ═══════════════════════════════════════════════════════════════

struct Parser {
    std::vector<Token> toks;
    size_t pos = 0;

    auto cur() -> const Token& { return toks[std::min(pos, toks.size()-1)]; }
    auto eat() -> Token { return std::move(toks[pos++]); }
    auto expect(Tok t) -> void {
        if (cur().type != t)
            throw std::runtime_error("expected " + std::to_string(int(t))
                + " got " + cur().text);
        eat();
    }
    auto match(Tok t) -> bool {
        if (cur().type == t) { eat(); return true; }
        return false;
    }

    // argument expression: does NOT consume comma as par
    auto p_arg() -> ExprPtr { return p_tilde(); }

    auto p_expr() -> ExprPtr { return p_tilde(); }

    auto p_tilde() -> ExprPtr {
        auto l = p_merge();
        while (cur().type == Tok::Tilde) { eat(); l = e_rec(l, p_merge()); }
        return l;
    }
    auto p_merge() -> ExprPtr {
        auto l = p_split();
        while (cur().type == Tok::MergeOp) { eat(); l = e_merge(l, p_split()); }
        return l;
    }
    auto p_split() -> ExprPtr {
        auto l = p_seq();
        while (cur().type == Tok::SplitOp) { eat(); l = e_split(l, p_seq()); }
        return l;
    }
    auto p_seq() -> ExprPtr {
        auto l = p_par();
        while (cur().type == Tok::Colon) { eat(); l = e_seq(l, p_par()); }
        return l;
    }
    auto p_par() -> ExprPtr {
        auto l = p_atom();
        while (cur().type == Tok::Comma) { eat(); l = e_par(l, p_atom()); }
        return l;
    }

    auto p_atom() -> ExprPtr {
        auto& t = cur();
        switch (t.type) {
        case Tok::Num:   { auto v = t.num; eat(); return e_num(v); }
        case Tok::Under: eat(); return e_wire();
        case Tok::Bang:  eat(); return e_cut();
        case Tok::Plus:  eat(); return e_binop("+");
        case Tok::Minus: eat(); return e_binop("-");
        case Tok::Slash: eat(); return e_binop("/");
        case Tok::Percent: eat(); return e_binop("%");
        case Tok::Caret: eat(); return e_binop("^");
        case Tok::Amp:   eat(); return e_binop("&");
        case Tok::Pipe:  eat(); return e_binop("|");
        case Tok::LShift: eat(); return e_binop("<<");
        case Tok::RShift: eat(); return e_binop(">>");
        case Tok::At:    eat(); return e_delay();
        case Tok::Star: {
            eat();
            if (cur().type == Tok::LP) {
                eat(); auto x = p_expr(); expect(Tok::RP);
                return e_seq(e_par(e_wire(), x), e_binop("*"));
            }
            return e_binop("*");
        }
        case Tok::LP: {
            eat(); auto x = p_expr(); expect(Tok::RP);
            return x;
        }
        case Tok::Id: {
            auto name = t.text;
            eat();

            // qualified name: a.b.c
            while (cur().type == Tok::Dot && pos+1 < toks.size()
                   && toks[pos+1].type == Tok::Id) {
                eat(); // dot
                name += "." + cur().text; eat();
            }

            // built-in unary primitives
            static const std::unordered_set<std::string> uops = {
                "sin","cos","tan","asin","acos","atan","sqrt",
                "exp","log","log10","abs","floor","ceil","rint",
                "int","float","neg"
            };
            if (uops.contains(name)) return e_unary(name);
            if (name == "mem") return e_mem();
            if (name == "select2") {
                auto e = std::make_shared<Expr>();
                e->kind = Expr::ESelect2;
                return e;
            }
            if (name == "min") return e_binop("min");
            if (name == "max") return e_binop("max");
            if (name == "atan2") return e_binop("atan2");
            if (name == "remainder") return e_binop("remainder");
            if (name == "xor") return e_binop("xor");

            // hslider / vslider
            if (name == "hslider" || name == "vslider") {
                expect(Tok::LP);
                auto label = cur().text; eat();
                expect(Tok::Comma);
                float init = cur().num; eat();
                expect(Tok::Comma);
                float lo = cur().num; eat();
                expect(Tok::Comma);
                float hi = cur().num; eat();
                expect(Tok::Comma);
                float step = cur().num; eat();
                expect(Tok::RP);
                return e_slider(name, label, init, lo, hi, step);
            }
            if (name == "button") {
                expect(Tok::LP);
                auto label = cur().text; eat();
                expect(Tok::RP);
                return e_button(label);
            }
            if (name == "checkbox") {
                expect(Tok::LP);
                auto label = cur().text; eat();
                expect(Tok::RP);
                return e_checkbox(label);
            }
            if (name == "nentry") {
                expect(Tok::LP);
                auto label = cur().text; eat();
                expect(Tok::Comma);
                float init = cur().num; eat();
                expect(Tok::Comma);
                float lo = cur().num; eat();
                expect(Tok::Comma);
                float hi = cur().num; eat();
                expect(Tok::Comma);
                float step = cur().num; eat();
                expect(Tok::RP);
                return e_nentry(label, init, lo, hi, step);
            }
            // UI groups
            if (name == "hgroup" || name == "vgroup" || name == "tgroup") {
                expect(Tok::LP);
                auto label = cur().text; eat();
                expect(Tok::Comma);
                auto body = p_expr();
                expect(Tok::RP);
                return e_group(name, label, body);
            }
            // declare — skip to semicolon
            if (name == "declare") {
                while (cur().type != Tok::Semi && cur().type != Tok::Eof) eat();
                return nullptr; // signal: skip this definition
            }
            // import — skip
            if (name == "import") {
                while (cur().type != Tok::Semi && cur().type != Tok::Eof) eat();
                return nullptr;
            }

            // function call: name(args)
            if (cur().type == Tok::LP) {
                eat();
                std::vector<ExprPtr> args;
                if (cur().type != Tok::RP) {
                    args.push_back(p_arg());
                    while (cur().type == Tok::Comma) {
                        eat(); args.push_back(p_arg());
                    }
                }
                expect(Tok::RP);
                return e_call(name, std::move(args));
            }

            // plain identifier
            return e_ident(name);
        }
        default:
            throw std::runtime_error("unexpected: " + cur().text);
        }
    }
};

inline auto register_pm(Env& env, float sr = 48000.f) -> void;

// ═══════════════════════════════════════════════════════════════
// §14  TOP-LEVEL PARSE — .dsp text → Env + process AST → Node
// ═══════════════════════════════════════════════════════════════

inline auto parse(std::string_view src) -> Node {
    Parser p;
    p.toks = tokenize(src);

    Env env;
    ExprPtr process_expr = e_wire();

    while (p.cur().type != Tok::Eof) {
        // skip declare
        if (p.cur().type == Tok::Id && p.cur().text == "declare") {
            while (p.cur().type != Tok::Semi && p.cur().type != Tok::Eof) p.eat();
            if (p.cur().type == Tok::Semi) p.eat();
            continue;
        }
        // import("lib") — register known libraries
        if (p.cur().type == Tok::Id && p.cur().text == "import") {
            p.eat();
            std::string lib;
            if (p.cur().type == Tok::LP) {
                p.eat();
                lib = p.cur().text; p.eat();
                if (p.cur().type == Tok::RP) p.eat();
            }
            if (p.cur().type == Tok::Semi) p.eat();
            if (lib == "stdfaust.lib" || lib == "pm.lib" || lib == "physmodels.lib")
                register_pm(env);
            continue;
        }

        // definition: name = expr ; OR name(args) = expr ;
        if (p.cur().type == Tok::Id && p.cur().text != "process") {
            auto name = p.cur().text; p.eat();

            // function definition: name(a, b, c) = expr ;
            if (p.cur().type == Tok::LP) {
                p.eat();
                std::vector<std::string> params;
                if (p.cur().type != Tok::RP) {
                    params.push_back(p.cur().text); p.eat();
                    while (p.cur().type == Tok::Comma) {
                        p.eat(); params.push_back(p.cur().text); p.eat();
                    }
                }
                p.expect(Tok::RP);
                p.expect(Tok::Eq);
                auto body = p.p_expr();
                if (p.cur().type == Tok::Semi) p.eat();
                env.funcs[name] = FuncDef{std::move(params), std::move(body)};
                continue;
            }

            // value definition: name = expr ;
            if (p.cur().type == Tok::Eq) {
                p.eat();
                auto expr = p.p_expr();
                if (p.cur().type == Tok::Semi) p.eat();
                env.vals[name] = std::move(expr);
                continue;
            }

            // skip unknown
            p.eat();
            continue;
        }

        // process = expr ;
        if (p.cur().type == Tok::Id && p.cur().text == "process") {
            p.eat();
            p.expect(Tok::Eq);
            process_expr = p.p_expr();
            if (p.cur().type == Tok::Semi) p.eat();
            continue;
        }

        p.eat(); // skip unknown
    }

    // eval the process expression with the environment
    return eval(process_expr, env);
}

// ═══════════════════════════════════════════════════════════════
// §15  RUNTIME — process a buffer. Zero allocation.
// ═══════════════════════════════════════════════════════════════

inline auto process_buffer(
    Signal& graph, State& state,
    std::span<const float> input,
    std::span<float> output,
    int n_samples) -> void
{
    int ni = graph.n_in();
    int no = graph.n_out();
    std::vector<float> in_frame(ni);
    std::vector<float> out_frame(no);

    for (int i = 0; i < n_samples; ++i) {
        // pack input frame
        for (int ch = 0; ch < ni; ++ch)
            in_frame[ch] = (ch == 0 && i < int(input.size())) ? input[i] : 0.f;

        graph.tick(state, in_frame, out_frame);

        // unpack output frame
        if (i < int(output.size()))
            output[i] = no > 0 ? out_frame[0] : 0.f;
    }
}

// Multi-channel version
inline auto process_buffer_mc(
    Signal& graph, State& state,
    std::span<const std::span<const float>> input,
    std::span<std::span<float>> output,
    int n_samples) -> void
{
    int ni = graph.n_in();
    int no = graph.n_out();
    std::vector<float> in_frame(ni);
    std::vector<float> out_frame(no);

    for (int i = 0; i < n_samples; ++i) {
        for (int ch = 0; ch < ni && ch < int(input.size()); ++ch)
            in_frame[ch] = i < int(input[ch].size()) ? input[ch][i] : 0.f;

        graph.tick(state, in_frame, out_frame);

        for (int ch = 0; ch < no && ch < int(output.size()); ++ch)
            if (i < int(output[ch].size()))
                output[ch][i] = out_frame[ch];
    }
}

// ═══════════════════════════════════════════════════════════════
// §16  PHYSICAL MODELING — pm.lib / physmodels.lib
//      Native C++ signal nodes. Zero FAUST dependency.
//      Algorithms: Karplus-Strong, Smith waveguide, modal synthesis.
//
//      Lisp via pointer chaining: each instrument is a closure.
//      NativeFunc in Env maps "pm.nylonGuitar" → Node factory.
//      Paste a .dsp in the browser, it runs.
// ═══════════════════════════════════════════════════════════════

// ── Noise source (xorshift32, per-node seed) ──

inline auto pm_noise(uint32_t& seed) -> float {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return float(int32_t(seed)) / 2147483647.f;
}

// ── Unit conversion ──

inline auto pm_l2s(float l, float sr) -> int {
    return std::max(1, int(l * sr / 340.f));
}
inline auto pm_f2l(float f) -> float {
    return 340.f / std::max(1.f, f);
}
inline auto pm_t60_decay(float seconds, float sr) -> float {
    return std::exp(-6.908f / (seconds * sr));
}

// ── Plucked string (Karplus-Strong extended) ──────────────────
// Inputs: gain(0), trigger(1). Output: audio(0).
// Const: stringLength (meters→samples), pluckPosition, damping.
// Covers: pm.nylonGuitar, pm.elecGuitar, pm.ks

struct PluckedStringNode final : Signal {
    int dlen;
    float damp_coeff, atten;
    std::vector<float> dl;
    int wp = 0;
    float lp_y1 = 0.f;
    float ap_x1[2] = {}, ap_y1[2] = {};
    float dc_x1 = 0.f, dc_y1 = 0.f;
    bool armed = false;
    uint32_t seed;

    PluckedStringNode(float stringLen, float sr,
                      float pluckPos = 0.5f, float damping = 0.5f)
        : dlen(pm_l2s(stringLen, sr))
        , damp_coeff(std::clamp(damping, 0.f, 0.99f))
        , atten(0.998f)
        , dl(dlen, 0.f)
        , seed(42 + dlen) {}

    auto tick(State&, std::span<const float> in,
              std::span<float> out) -> void override {
        float gain = in.size() > 0 ? in[0] : 0.8f;
        float trig = in.size() > 1 ? in[1] : 0.f;

        // Exciter: fill delay with noise on trigger rising edge
        if (trig > 0.5f && !armed) {
            armed = true;
            for (int i = 0; i < dlen; ++i)
                dl[i] = pm_noise(seed) * gain;
        }
        if (trig < 0.5f) armed = false;

        // Read current sample
        float x = dl[wp];

        // 2-stage allpass dispersion (string stiffness)
        constexpr float g = 0.5f;
        for (int i = 0; i < 2; ++i) {
            float y = -g * x + ap_x1[i] + g * ap_y1[i];
            ap_x1[i] = x; ap_y1[i] = y;
            x = y;
        }

        // One-pole lowpass (damping per round trip)
        x = (1.f - damp_coeff) * x + damp_coeff * lp_y1;
        lp_y1 = x;

        // Round-trip attenuation
        x *= atten;

        // DC blocker
        float dc = x - dc_x1 + 0.995f * dc_y1;
        dc_x1 = x; dc_y1 = dc;

        // Write back into delay line
        dl[wp] = dc;
        wp = (wp + 1) % dlen;
        out[0] = dc;
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// ── Bowed string (waveguide pair + bow table) ─────────────────
// Inputs: bowVelocity(0), bowPressure(1). Output: audio(0).
// Const: stringLength, bowPosition.
// Covers: pm.violin

struct BowedStringNode final : Signal {
    int nut_len, bridge_len;
    std::vector<float> nut_dl, bridge_dl;
    int nut_wp = 0, bridge_wp = 0;
    float body_lp = 0.f;

    BowedStringNode(float stringLen, float sr, float bowPos) {
        int total = std::max(4, pm_l2s(stringLen, sr));
        float bp = std::clamp(bowPos, 0.05f, 0.95f);
        nut_len = std::max(2, int(total * bp));
        bridge_len = std::max(2, total - nut_len);
        nut_dl.resize(nut_len, 0.f);
        bridge_dl.resize(bridge_len, 0.f);
    }

    static auto bow_table(float v_diff, float pressure) -> float {
        float slope = 5.f * std::max(0.01f, pressure);
        float x = v_diff + 0.2f;
        return slope * x * std::exp(-slope * x * x + 0.5f);
    }

    auto tick(State&, std::span<const float> in,
              std::span<float> out) -> void override {
        float bow_vel  = in.size() > 0 ? in[0] : 0.f;
        float bow_press = in.size() > 1 ? in[1] : 0.f;

        // Waves returning to bow point after round-trip + reflection
        int nr = (nut_wp - 1 + nut_len) % nut_len;
        int br = (bridge_wp - 1 + bridge_len) % bridge_len;
        float from_nut    = -nut_dl[nr];               // nut: inverting reflection
        float from_bridge = bridge_dl[br] * -0.95f;    // bridge: lossy inverting

        // String velocity at bow
        float v_string = from_nut + from_bridge;
        float force = bow_table(bow_vel - v_string, bow_press);

        // Outgoing waves from bow point
        nut_dl[nut_wp] = from_bridge + force;
        bridge_dl[bridge_wp] = from_nut + force;
        nut_wp = (nut_wp + 1) % nut_len;
        bridge_wp = (bridge_wp + 1) % bridge_len;

        // Body coupling (listen at bridge)
        float raw = from_nut + force;
        body_lp = 0.15f * raw + 0.85f * body_lp;
        out[0] = body_lp;
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// ── Flute (jet-driven tube) ───────────────────────────────────
// Inputs: pressure(0), breathNoise(1). Output: audio(0).
// Const: tubeLength, mouthPosition.

struct FluteNode final : Signal {
    int bore_len, jet_len;
    std::vector<float> bore_dl, jet_dl;
    int bore_wp = 0, jet_wp = 0;
    float lp = 0.f;
    uint32_t seed;

    FluteNode(float tubeLen, float sr, float mouthPos)
        : bore_len(std::max(2, pm_l2s(tubeLen, sr)))
        , jet_len(std::max(1, int(bore_len * std::clamp(mouthPos, 0.05f, 0.5f))))
        , bore_dl(bore_len, 0.f), jet_dl(jet_len, 0.f)
        , seed(7919) {}

    auto tick(State&, std::span<const float> in,
              std::span<float> out) -> void override {
        float pressure = in.size() > 0 ? in[0] : 0.f;
        float breath   = in.size() > 1 ? in[1] : 0.05f;

        // Jet: pressure + breath noise → delay → cubic nonlinearity
        float jet_in  = pressure + pm_noise(seed) * breath;
        float jet_out = jet_dl[jet_wp];
        jet_dl[jet_wp] = jet_in;
        jet_wp = (jet_wp + 1) % jet_len;

        float jet_nl = jet_out - (jet_out * jet_out * jet_out) / 3.f;

        // Bore: open-end reflection (inverted, lowpassed)
        int rp = (bore_wp - 1 + bore_len) % bore_len;
        float bore_reflect = bore_dl[rp];
        float reflect = -0.7f * bore_reflect;
        lp = 0.3f * reflect + 0.7f * lp;

        bore_dl[bore_wp] = jet_nl + lp * 0.35f;
        bore_wp = (bore_wp + 1) % bore_len;

        out[0] = bore_reflect * 0.3f;
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// ── Clarinet (single-reed bore) ───────────────────────────────
// Inputs: pressure(0), reedStiffness(1). Output: audio(0).
// Const: tubeLength.

struct ClarinetNode final : Signal {
    int bore_len;
    std::vector<float> bore_dl;
    int bore_wp = 0;
    float lp = 0.f;

    ClarinetNode(float tubeLen, float sr)
        : bore_len(std::max(2, pm_l2s(tubeLen, sr)))
        , bore_dl(bore_len, 0.f) {}

    auto tick(State&, std::span<const float> in,
              std::span<float> out) -> void override {
        float pressure   = in.size() > 0 ? in[0] : 0.f;
        float reed_stiff = in.size() > 1 ? in[1] : 0.5f;

        // Returning wave from bore
        int rp = (bore_wp - 1 + bore_len) % bore_len;
        float bore_out = bore_dl[rp];

        // Reed: pressure difference → aperture → flow
        float delta_p = pressure - bore_out;
        float reed = std::clamp(-reed_stiff * delta_p + 0.7f, 0.f, 1.f);
        float flow = reed * delta_p;

        // Bell reflection (lowpass, partial)
        lp = 0.4f * bore_out + 0.6f * lp;

        bore_dl[bore_wp] = flow + lp * 0.35f;
        bore_wp = (bore_wp + 1) % bore_len;

        out[0] = (1.f - reed) * bore_out * 0.5f;
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// ── Modal bar (struck resonator) ──────────────────────────────
// Inputs: gain(0), trigger(1). Output: audio(0).
// Const: freq, mode ratios/decays/gains.
// Covers: pm.marimba, pm.djembe, pm.modalBar

struct ModalBarNode final : Signal {
    static constexpr int MAX_MODES = 8;
    int n_modes;
    float sr;
    float mode_freq[MAX_MODES];
    float mode_decay[MAX_MODES];
    float mode_init[MAX_MODES];
    float mode_amp[MAX_MODES] = {};
    float mode_phase[MAX_MODES] = {};
    bool armed = false;

    ModalBarNode(float f, float sr_, const float* ratios,
                 const float* decays, const float* gains, int nm)
        : n_modes(std::min(nm, MAX_MODES)), sr(sr_)
    {
        for (int i = 0; i < n_modes; ++i) {
            mode_freq[i]  = f * ratios[i];
            mode_decay[i] = decays[i];
            mode_init[i]  = gains[i];
        }
    }

    auto tick(State&, std::span<const float> in,
              std::span<float> out) -> void override {
        float gain = in.size() > 0 ? in[0] : 0.8f;
        float trig = in.size() > 1 ? in[1] : 0.f;

        if (trig > 0.5f && !armed) {
            armed = true;
            for (int i = 0; i < n_modes; ++i) {
                mode_amp[i]   = gain * mode_init[i];
                mode_phase[i] = 0.f;
            }
        }
        if (trig < 0.5f) armed = false;

        constexpr float TWO_PI = 6.2831853f;
        float sum = 0.f;
        for (int i = 0; i < n_modes; ++i) {
            sum += mode_amp[i] * std::sin(mode_phase[i]);
            mode_phase[i] += TWO_PI * mode_freq[i] / sr;
            if (mode_phase[i] > TWO_PI) mode_phase[i] -= TWO_PI;
            mode_amp[i] *= mode_decay[i];
        }
        out[0] = sum;
    }
    auto n_in()  const -> int override { return 2; }
    auto n_out() const -> int override { return 1; }
};

// ═══════════════════════════════════════════════════════════════
// §17  LIBRARY REGISTRATION — register_pm populates Env
//      Called when import("pm.lib") or import("physmodels.lib").
// ═══════════════════════════════════════════════════════════════

inline auto register_pm(Env& env, float sr) -> void {
    // ── Constants ──
    env.vals["ma.SR"] = e_num(sr);
    env.vals["ma.PI"] = e_num(3.14159265358979f);
    env.vals["ma.T"]  = e_num(1.f / sr);

    auto t60 = [sr](float seconds) -> float {
        return pm_t60_decay(seconds, sr);
    };

    // ── pm.l2s(l) — meters to samples ──
    env.natives["pm.l2s"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        if (args.empty()) return mk_const(0.f);
        auto cv = try_const_eval(args[0], e);
        if (cv) return mk_const(float(pm_l2s(*cv, sr)));
        return mk_seq(eval(args[0], e),
            mk_seq(mk_par(mk_wire(), mk_const(sr / 340.f)),
                   mk_binop(BinOp::Mul)));
    };

    // ── pm.f2l(f) — frequency to wavelength ──
    env.natives["pm.f2l"] = [](const std::vector<ExprPtr>& args, Env& e) -> Node {
        if (args.empty()) return mk_const(0.f);
        auto cv = try_const_eval(args[0], e);
        if (cv) return mk_const(pm_f2l(*cv));
        return mk_seq(eval(args[0], e),
            mk_seq(mk_par(mk_const(340.f), mk_wire()),
                   mk_binop(BinOp::Div)));
    };

    // ── pm.nylonGuitar(stringLength, pluckPosition, gain, trigger) ──
    env.natives["pm.nylonGuitar"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float slen = 0.65f, ppos = 0.5f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) slen = *v; }
        if (args.size() > 1) { auto v = try_const_eval(args[1], e); if (v) ppos = *v; }

        auto guitar = std::make_unique<PluckedStringNode>(slen, sr, ppos, 0.5f);

        Node g = args.size() > 2 ? eval(args[2], e) : mk_const(0.8f);
        Node t = args.size() > 3 ? eval(args[3], e) : mk_const(0.f);
        return mk_seq(mk_par(std::move(g), std::move(t)), std::move(guitar));
    };

    // ── pm.elecGuitar(stringLength, pluckPosition, gain, trigger) ──
    env.natives["pm.elecGuitar"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float slen = 0.65f, ppos = 0.13f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) slen = *v; }
        if (args.size() > 1) { auto v = try_const_eval(args[1], e); if (v) ppos = *v; }

        auto guitar = std::make_unique<PluckedStringNode>(slen, sr, ppos, 0.3f);

        Node g = args.size() > 2 ? eval(args[2], e) : mk_const(0.8f);
        Node t = args.size() > 3 ? eval(args[3], e) : mk_const(0.f);
        return mk_seq(mk_par(std::move(g), std::move(t)), std::move(guitar));
    };

    // ── pm.ks(stringLength, damping) — basic Karplus-Strong ──
    //    2 inputs: gain(0), trigger(1)
    env.natives["pm.ks"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float slen = 0.65f, damp = 0.5f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) slen = *v; }
        if (args.size() > 1) { auto v = try_const_eval(args[1], e); if (v) damp = *v; }
        return std::make_unique<PluckedStringNode>(slen, sr, 0.5f, damp);
    };

    // ── pm.violin(stringLength, bowPressure, bowVelocity, bowPosition) ──
    env.natives["pm.violin"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float slen = 0.5f, bpos = 0.12f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) slen = *v; }
        if (args.size() > 3) { auto v = try_const_eval(args[3], e); if (v) bpos = *v; }

        auto violin = std::make_unique<BowedStringNode>(slen, sr, bpos);

        // Signal inputs: bowVelocity, bowPressure
        Node vel   = args.size() > 2 ? eval(args[2], e) : mk_const(0.3f);
        Node press = args.size() > 1 ? eval(args[1], e) : mk_const(0.5f);
        return mk_seq(mk_par(std::move(vel), std::move(press)), std::move(violin));
    };

    // ── pm.flute(tubeLength, mouthPosition, pressure, breathGain, breathCutoff) ──
    env.natives["pm.flute"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float tlen = 0.5f, mpos = 0.2f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) tlen = *v; }
        if (args.size() > 1) { auto v = try_const_eval(args[1], e); if (v) mpos = *v; }

        auto flute = std::make_unique<FluteNode>(tlen, sr, mpos);

        Node press  = args.size() > 2 ? eval(args[2], e) : mk_const(0.5f);
        Node breath = args.size() > 3 ? eval(args[3], e) : mk_const(0.05f);
        return mk_seq(mk_par(std::move(press), std::move(breath)), std::move(flute));
    };

    // ── pm.clarinet(tubeLength, pressure, reedStiffness, bellOpening) ──
    env.natives["pm.clarinet"] = [sr](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float tlen = 0.5f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) tlen = *v; }

        auto clar = std::make_unique<ClarinetNode>(tlen, sr);

        Node press = args.size() > 1 ? eval(args[1], e) : mk_const(0.5f);
        Node reed  = args.size() > 2 ? eval(args[2], e) : mk_const(0.5f);
        return mk_seq(mk_par(std::move(press), std::move(reed)), std::move(clar));
    };

    // ── pm.marimba(freq, strikePos, strikeCutoff, strikeSharpness, gain, trigger) ──
    env.natives["pm.marimba"] = [sr, t60](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float freq = 440.f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) freq = *v; }

        // Marimba modes: 1:4:9.2:16.5:24.4 (measured, inharmonic bar)
        float ratios[] = {1.f, 3.984f, 9.723f, 16.55f, 24.36f};
        float decays[] = {t60(3.f), t60(2.f), t60(1.f), t60(0.5f), t60(0.3f)};
        float gains[]  = {1.f, 0.6f, 0.3f, 0.15f, 0.08f};

        auto bar = std::make_unique<ModalBarNode>(freq, sr, ratios, decays, gains, 5);

        // Use last two args as gain/trigger if 6 args, else args 1,2
        Node gain, trig;
        if (args.size() >= 6) {
            gain = eval(args[4], e); trig = eval(args[5], e);
        } else {
            gain = args.size() > 1 ? eval(args[1], e) : mk_const(0.8f);
            trig = args.size() > 2 ? eval(args[2], e) : mk_const(0.f);
        }
        return mk_seq(mk_par(std::move(gain), std::move(trig)), std::move(bar));
    };

    // ── pm.djembe(freq, strikePosition, strikeGain, trigger) ──
    env.natives["pm.djembe"] = [sr, t60](const std::vector<ExprPtr>& args, Env& e) -> Node {
        float freq = 60.f;
        if (args.size() > 0) { auto v = try_const_eval(args[0], e); if (v) freq = *v; }

        // Membrane modes (roughly harmonic)
        float ratios[] = {1.f, 1.59f, 2.14f, 2.65f, 3.16f, 3.65f};
        float decays[] = {t60(1.f), t60(0.7f), t60(0.4f), t60(0.25f),
                          t60(0.15f), t60(0.1f)};
        float gains[]  = {1.f, 0.8f, 0.5f, 0.3f, 0.2f, 0.12f};

        auto drum = std::make_unique<ModalBarNode>(freq, sr, ratios, decays, gains, 6);

        Node gain = args.size() > 2 ? eval(args[2], e) : mk_const(0.8f);
        Node trig = args.size() > 3 ? eval(args[3], e) : mk_const(0.f);
        return mk_seq(mk_par(std::move(gain), std::move(trig)), std::move(drum));
    };
}

} // namespace dsp
