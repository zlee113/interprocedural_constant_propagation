#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>
#include <stack>

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace
{
    /* Enum to rep the different kinds of values something can pass as */
    enum class Kind
    {
        Top,   /* Unknown */
        Const, /* Definitely a constant */
        Bottom /* Not a constant */
    }; // Top=unknown, Bottom=NAC

    /**
     * @brief Struct for values
     */
    struct LVal
    {
        /* Defaults to unknown */
        Kind kind = Kind::Top;
        int64_t c = 0;
        /* Function to get the top value */
        static LVal top() { return {Kind::Top, 0}; }
        /* Function to get the value for the constant */
        static LVal constant(int64_t v) { return {Kind::Const, v}; }
        /* Function to get the value for bottom */
        static LVal bottom() { return {Kind::Bottom, 0}; }
        /* Comparison operator for equals */
        bool operator==(const LVal &o) const { return kind == o.kind && c == o.c; }
        /* Comparison operator for not equals */
        bool operator!=(const LVal &o) const { return !(*this == o); }
    };

    /* Creates the map for a value to a LVal */
    using CPState = DenseMap<const Value *, LVal>;
    /* Makes the block for each in and out */
    struct BlockState
    {
        CPState in;
        CPState out;
    };

    /**
     * @brief Meet function for constant propagation
     *
     * @param a LVal to use for meet eval
     * @param b LVal to use for meet eval
     * @return LVal resulting lval after meet operation
     */
    static LVal meetVal(LVal a, LVal b)
    {
        /* If a is unknown return b */
        if (a.kind == Kind::Top)
            return b;
        /* If b is unknown return a */
        if (b.kind == Kind::Top)
            return a;
        /* If a or b is bottom return bottom */
        if (a.kind == Kind::Bottom || b.kind == Kind::Bottom)
            return LVal::bottom();
        /* If a val equals b val return a or return bottom */
        return (a.c == b.c) ? a : LVal::bottom();
    }

    /**
     * @brief Converts a value into the struct LVal
     *
     * @param V The value being transformed
     * @param st The map
     * @return LVal Returns an LVal based on what enum the value represents
     */
    static LVal evalValue(const Value *V, const CPState &st)
    {
        if (const auto *CI = dyn_cast<ConstantInt>(V))
            return LVal::constant(CI->getSExtValue());
        auto it = st.find(V);
        if (it == st.end())
            return LVal::top();
        return it->second;
    }

    /**
     * @brief Evaluates a binary operation
     *
     * @param BO The binary operation in question
     * @param st The map
     * @return LVal Returns the LVal based on what the operands give
     */
    static LVal evalBinary(const BinaryOperator &BO, const CPState &st)
    {
        LVal l = evalValue(BO.getOperand(0), st);
        LVal r = evalValue(BO.getOperand(1), st);
        if (l.kind == Kind::Bottom || r.kind == Kind::Bottom)
            return LVal::bottom();
        if (l.kind != Kind::Const || r.kind != Kind::Const)
            return LVal::top();

        if (BO.getOpcode() == Instruction::Add)
            return LVal::constant(l.c + r.c);
        else if (BO.getOpcode() == Instruction::Sub)
            return LVal::constant(l.c - r.c);
        else if (BO.getOpcode() == Instruction::Mul)
            return LVal::constant(l.c * r.c);
        else if (BO.getOpcode() == Instruction::SDiv)
            return LVal::constant(l.c / r.c);
        return LVal::top();
    }

    /**
     * @brief Evaluates a phi operation
     *
     * @param Phi The phi operation in question
     * @param states The entire map
     * @return LVal The LVal based on the phi operation
     */
    static LVal evalPhi(const PHINode &Phi, const DenseMap<const BasicBlock *, BlockState> &states)
    {
        /* Get the number of incoming values to loop through */
        unsigned int num_values = Phi.getNumIncomingValues();

        /* Create return variable and default it to top */
        LVal final_val = LVal::top();

        /* Loop over the incoming values */
        for (int i = 0; i < num_values; ++i)
        {
            /* Get incoming block */
            BasicBlock *BB = Phi.getIncomingBlock(i);
            /* Get the incoming values and convert to lval for comparison */
            Value *val = Phi.getIncomingValue(i);
            LVal converted_val = evalValue(val, states.find(BB)->second.out);
            /* Get the meet for this new val and the final val*/
            final_val = meetVal(final_val, converted_val);
        }

        /* If the variable is a loop variable it can cause an infinite loop if the values inherit from themselves so we need to meet with the last iterations value as well to amke sure we aren't flip flopping between iterations */
        auto phi_state = states.find(Phi.getParent());
        final_val = meetVal(final_val, evalValue(&Phi, phi_state->second.out));
        return final_val;
    }

    /**
     * @brief The transfer function for computing an entire basic blocks out
     *
     * @param BB The entire basic block
     * @param in The ansector for this
     * @param states The entire map
     * @return CPState
     */
    static CPState transferBlock(BasicBlock &BB, const CPState &in,
                                 const DenseMap<const BasicBlock *, BlockState> &states)
    {
        CPState out = in;
        for (Instruction &I : BB)
        {
            if (I.getType()->isVoidTy())
                continue;

            if (auto *P = dyn_cast<PHINode>(&I))
            {
                out[&I] = evalPhi(*P, states);
            }
            else if (auto *BO = dyn_cast<BinaryOperator>(&I))
            {
                out[&I] = evalBinary(*BO, out);
            }
            else
            {
                out[&I] = LVal::top();
            }
        }
        return out;
    }

    /**
     * @brief The module pass to do the full inter constant propagation pass for
     */
    struct InterConstPropPass : PassInfoMixin<InterConstPropPass>
    {
        static bool sameState(const CPState &a, const CPState &b, const std::vector<const Value *> &domain)
        {
            for (const Value *V : domain)
            {
                LVal av = a.lookup(V);
                LVal bv = b.lookup(V);
                if (av != bv)
                    return false;
            }
            return true;
        }

        static void printState(raw_ostream &OS, StringRef label, const CPState &st,
                               const std::vector<const Value *> &domain, bool showTop = true)
        {
            OS << "  " << label << ": { ";
            bool first = true;
            for (const Value *V : domain)
            {
                LVal v = st.lookup(V);
                if (!showTop && v.kind == Kind::Top)
                    continue;
                if (!first)
                    OS << "; ";
                first = false;
                V->printAsOperand(OS, false);
                if (v.kind == Kind::Const)
                    OS << " = " << v.c;
                else if (v.kind == Kind::Bottom)
                    OS << " = NAC";
                else
                    OS << " = TOP";
            }
            OS << " }\n";
        }

        /* The function pass for const prop (UNCHANGED AS OF RIGHT NOW)*/
        DenseMap<const BasicBlock *, BlockState> intra_function_run(Function &F)
        {
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";

            std::vector<const Value *> domain;
            for (auto &BB : F)
            {
                for (auto &I : BB)
                {
                    if (!I.getType()->isVoidTy())
                        domain.push_back(&I);
                }
            }

            std::vector<BasicBlock *> order;
            order.push_back(&F.getEntryBlock());
            for (size_t i = 0; i < order.size(); ++i)
            {
                for (BasicBlock *succ : successors(order[i]))
                {
                    if (std::find(order.begin(), order.end(), succ) == order.end())
                        order.push_back(succ);
                }
            }

            DenseMap<const BasicBlock *, BlockState> st;
            for (BasicBlock *BB : order)
            {
                BlockState bs;
                for (const Value *V : domain)
                {
                    bs.in[V] = LVal::top();
                    bs.out[V] = LVal::top();
                }
                st[BB] = std::move(bs);
            }

            bool changed = true;
            while (changed)
            {
                changed = false;
                for (BasicBlock *BB : order)
                {
                    CPState newIn;
                    for (const Value *V : domain)
                        newIn[V] = LVal::top();

                    bool hasPred = false;
                    for (BasicBlock *pred : predecessors(BB))
                    {
                        hasPred = true;
                        for (const Value *V : domain)
                            newIn[V] = meetVal(newIn.lookup(V), st[pred].out.lookup(V));
                    }
                    if (!hasPred)
                    {
                        for (const Value *V : domain)
                            newIn[V] = LVal::top();
                    }

                    st[BB].in = newIn;
                    CPState newOut = transferBlock(*BB, newIn, st);
                    if (!sameState(st[BB].out, newOut, domain))
                    {
                        st[BB].out = std::move(newOut);
                        changed = true;
                    }
                }
            }

            for (BasicBlock *BB : order)
            {
                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printState(outs(), "IN", st[BB].in, domain);
                printState(outs(), "OUT", st[BB].out, domain);
            }
            return st;
        }

        /**
         * @brief The summary contains the lval for the parameters passed into the function as well as the return value
         */
        struct summary_t
        {
            std::vector<LVal> params;      /**> Vector of the parameters LVal */
            LVal return_val = LVal::top(); /**> Return values lval (defaulted to top) */
        };

        /**
         * @brief Get the summary return object
         *
         * @param F The function to get the return for
         * @param st The block state of the functions basic blocks via the itnra const prop pass
         * @return summary_t The return summary field filled out
         */
        summary_t
        get_summary_return(Function &F, DenseMap<const BasicBlock *, BlockState> st)
        {
            /* Iterate over the instructions and find any returns */
            summary_t summary = {};
            for (BasicBlock &BB : F)
            {
                for (Instruction &I : BB)
                {
                    if (auto *return_inst = dyn_cast<ReturnInst>(&I))
                    {
                        /* Evaluate the return and meet it with any prior or future returns */
                        LVal return_lval = evalValue(return_inst, st.at(&BB).out);
                        summary.return_val = meetVal(return_lval, summary.return_val);
                    }
                }
            }
            return summary;
        }

        /* The run function for the module*/
        PreservedAnalyses
        run(Module &M, ModuleAnalysisManager &AM)
        {
            /* Build call graph to figure out how to iterate over the functions so we can use the call graph to determine which functions call which and do the propagation in the right order */
            /* Not sure if we can just use this one or if we need our own, if we need our own than this is TODO as well*/
            CallGraph &CG = AM.getResult<CallGraphAnalysis>(M);
            DenseMap<const Function *, summary_t> function_summaries;

            /* TODO 1: Iterate over the call graph and get summaries of each function, each summary will be the intra const prop run where it determines the state (TOP,const,BOT) for the arguments passed into the function and the return. Looks like scc will be required for iterating and will help with recusive functions */
            DenseMap<const BasicBlock *, BlockState> function_state;
            /* Iterate over the functions from the call graph via scc iterators */
            for (auto it = scc_begin(&CG); it != scc_end(&CG); ++it)
            {
                const std::vector<CallGraphNode *> &SCC = *it;
                for (CallGraphNode *CGN : SCC)
                {
                    Function *F = CGN->getFunction();
                    /* Call graph has null function pointer that segfauls skip it*/
                    if (!F || F->isDeclaration())
                        continue;

                    function_state = intra_function_run(*F);
                    function_summaries[F] = get_summary_return(*F, function_state);
                    outs()
                        << F->getName() << " return_val: ";
                    if (function_summaries[F].return_val.kind == Kind::Const)
                        outs() << "Const(" << function_summaries[F].return_val.c << ")\n";
                    else if (function_summaries[F].return_val.kind == Kind::Top)
                        outs() << "Top\n";
                    else
                        outs() << "Bottom\n";
                }
            }
            /* Needs before this run function is edited:
            - Create summary struct with states for args and return
            - Edit intra run function to take in summary and fill out those field
            */
            /* Steps for this function:
            - Initialize summary
            - Iterate over call graph and pass in summary
            */
            /* NOTE: Will probably be easier to implement this separating the arguments and return into two different functions */
            /* NOTE2: Not sure exactly, seems like it might need multiple passes at first (maybe should be a while loop checking summaries for no changes and then ends)*/

            /* TODO 2: Iterate top down with summaries param values on intra run function again */
            /* This will involve rerunning the intra function to get the state now that the parameters are not all top like they would've been for the first pass*/
            /* The second part will be checking all the call sites for constants */

            /* TODO 3: Do the actual folding for the constants (Should be able to pull a lot of this and do it on a function level)*/

            return PreservedAnalyses::all();
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo
llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "Inter_const_prop", "v0.1",
            [](PassBuilder &PB)
            {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>) -> bool
                    {
                        if (Name == "interconstprop")
                        {
                            MPM.addPass(InterConstPropPass());
                            return true;
                        }
                        return false;
                    });
            }};
}