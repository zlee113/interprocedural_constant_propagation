CXX          = clang++
CXXFLAGS     = -rdynamic $(shell llvm-config --cxxflags) -fPIC -g -std=c++20
LDFLAGS      = $(shell llvm-config --ldflags | tr '\n' ' ') -Wl,--exclude-libs,ALL
BUILDDIR     = build
DEPDIR       = $(BUILDDIR)/.deps
DEPFLAGS     = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

# one .so per pass
INTER_LIB  = $(BUILDDIR)/interConstPropPass.so
INTRA_LIB  = $(BUILDDIR)/intraConstPropPass.so

# picks up any .bc in tests/
TESTS      = $(patsubst tests/%.bc,%,$(wildcard tests/*.bc))

# outputs per pass
TESTS_M2R   = $(TESTS:%=$(BUILDDIR)/tests/%-m2r.ll)
TESTS_INTER = $(TESTS:%=$(BUILDDIR)/tests/%-inter-opt.ll)
TESTS_INTRA = $(TESTS:%=$(BUILDDIR)/tests/%-intra-opt.ll)

DEPFILES = interConstPropPass.cpp intraConstPropPass.cpp
DEPFILES := $(DEPFILES:%.cpp=$(DEPDIR)/%.d)

.PHONY: all clean tests inter intra
.SECONDARY:

all: $(INTER_LIB) $(INTRA_LIB)

tests: $(TESTS_M2R) $(TESTS_INTER) $(TESTS_INTRA)

inter: $(TESTS_M2R) $(TESTS_INTER)

intra: $(TESTS_M2R) $(TESTS_INTRA)

clean:
	rm -rf $(BUILDDIR)

# compile C to bc
tests/%.bc: tests/%.c
	clang -fno-discard-value-names -O0 -Xclang -disable-O0-optnone -emit-llvm -c $< -o $@-tmp.bc
	opt -passes='mem2reg' $@-tmp.bc -o $@
	rm $@-tmp.bc

# compile each .cpp to .o
$(BUILDDIR)/%.o: %.cpp $(DEPDIR)/%.d | $(DEPDIR) $(BUILDDIR)
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

# link each .o to its own .so
$(BUILDDIR)/%.so: $(BUILDDIR)/%.o
	$(CXX) -shared $^ -o $@ $(LDFLAGS)

# inter pass output
$(BUILDDIR)/tests/%-inter-opt.bc: tests/%.bc $(INTER_LIB) | $(BUILDDIR)/tests
	opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin=$(INTER_LIB) -passes='interconstprop' $< -o $@

# intra pass output
$(BUILDDIR)/tests/%-intra-opt.bc: tests/%.bc $(INTRA_LIB) | $(BUILDDIR)/tests
	opt -bugpoint-enable-legacy-pm=1 -load-pass-plugin=$(INTRA_LIB) -passes='intraconstprop' $< -o $@

# disassemble to ll
$(BUILDDIR)/tests/%-inter-opt.ll: $(BUILDDIR)/tests/%-inter-opt.bc
	llvm-dis $< -o $@

$(BUILDDIR)/tests/%-intra-opt.ll: $(BUILDDIR)/tests/%-intra-opt.bc
	llvm-dis $< -o $@

$(BUILDDIR)/tests/%-m2r.ll: tests/%.bc | $(BUILDDIR)/tests
	llvm-dis $< -o $@

$(DEPDIR) $(BUILDDIR) $(BUILDDIR)/tests:
	@mkdir -p $@

$(DEPFILES):
-include $(wildcard $(DEPFILES))