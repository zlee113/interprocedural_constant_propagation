#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>
#include <stack>

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace
{
    struct ConstantPropPass : PassInfoMixin<ConstantPropPass>
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

            // Starter example: only Add is implemented.
            // TODO(student): extend to Sub/Mul/Div and policy for unsupported ops.
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
            // TODO(student): merge incoming values from predecessor OUT states.
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
                    // TODO(student): handle icmp/select/loads/stores etc.
                    out[&I] = LVal::top();
                }
            }
            return out;
        }

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

        PreservedAnalyses run(Function &F, FunctionAnalysisManager &)
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
            int constants_discovered = 0;
            int instructions_eliminated = 0;
            int dead_branches_folded = 0;

            /* Do folding for each constant expression too */
            std::vector<Instruction *> inst_to_delete;
            for (auto &BB : F)
            {
                for (auto &I : BB)
                {
                    if (I.getType()->isVoidTy())
                        continue;
                    if (isa<CallInst>(I))
                        continue;

                    LVal val = st.at(&BB).out.lookup(&I);
                    if (val.kind == Kind::Const)
                    {
                        Value *const_val = ConstantInt::getSigned(I.getType(), val.c);
                        I.replaceAllUsesWith(const_val);
                        /* Add the instruction to ones to delete later */
                        inst_to_delete.push_back(&I);
                    }
                }
            }
            /* Actually delete unused instructions */
            for (Instruction *I : inst_to_delete)
            {
                I->eraseFromParent();
                constants_discovered++;
                instructions_eliminated++;
            }

            outs() << "  Constants discovered:    " << constants_discovered << "\n";
            outs() << "  Instructions eliminated: " << instructions_eliminated << "\n";
            outs() << "  Dead branches folded:    " << dead_branches_folded << "\n";
            for (BasicBlock *BB : order)
            {
                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printState(outs(), "IN", st[BB].in, domain);
                printState(outs(), "OUT", st[BB].out, domain);
            }

            return PreservedAnalyses::none();
        }
    };

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "intraConstPropPass", "v1.0", [](PassBuilder &PB)
            {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) -> bool
                    {
                        if (Name == "intraconstprop")
                        {
                            FPM.addPass(ConstantPropPass());
                            return true;
                        }
                        return false;
                    });
            }};
}