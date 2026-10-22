// Inspects the nested session-preview display-state builder and its direct call graph.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class InspectWechatSessionPreviewTextBuilder extends GhidraScript {
    private static final String TARGET = "181949b30";

    @Override
    protected void run() throws Exception {
        Address targetAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(TARGET);
        FunctionManager functions = currentProgram.getFunctionManager();
        Function target = functions.getFunctionAt(targetAddress);
        if (target == null) {
            throw new IllegalStateException("Target function is missing: " + TARGET);
        }

        Set<Function> selected = new LinkedHashSet<>();
        selected.add(target);

        ReferenceIterator callers = currentProgram.getReferenceManager().getReferencesTo(targetAddress);
        while (callers.hasNext()) {
            Function caller = functions.getFunctionContaining(callers.next().getFromAddress());
            if (caller != null) selected.add(caller);
        }

        println("TARGET_CALLEE_COUNT|" + target.getCalledFunctions(monitor).size());

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            for (Function function : selected) {
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" +
                    function.getName(true) + "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
