// Decompiles the service callbacks registered by FUN_18195b500.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class DecompileWechatServiceCallbacks extends GhidraScript {
    private static final String[] ENTRIES = {
        "18194b950",
        "18194b970",
        "18194bac0",
        "18194c3c0",
        "18194ce30",
        "18194d9d0",
        "18194d0a0",
        "18194f130",
        "181951f30",
        "181955c80",
        "181951a80",
        "181950e50",
        "181951200",
        "18194a7d0",
        "18194a840",
        "18194c100",
    };

    @Override
    protected void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (String entry : ENTRIES) {
                Function function = currentProgram.getFunctionManager().getFunctionContaining(toAddr(entry));
                if (function == null) {
                    println("FUNCTION_MISSING|entry=" + entry);
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|requested=" + entry + "|entry=" + function.getEntryPoint() +
                    "|name=" + function.getName() + "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                    completed += 1;
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
        println("SUMMARY|requested=" + ENTRIES.length + "|completed=" + completed);
    }
}
