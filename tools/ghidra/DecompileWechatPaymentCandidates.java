// Decompiles the Weixin payment-related candidate functions found by exact string xrefs.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatPaymentCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "180e88fd0",
        "182292ce0",
        "182293980",
        "1852814a0",
        "185297710",
        "18529ed20",
        "1852a2c80"
    );

    @Override
    protected void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            for (String entry : ENTRIES) {
                if (monitor.isCancelled()) break;
                Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(entry);
                Function function = currentProgram.getListing().getFunctionAt(address);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(address);
                if (function == null) {
                    println("FUNCTION_MISSING|requested=" + entry);
                    continue;
                }

                DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
                println(
                    "FUNCTION_BEGIN|requested=" + entry +
                    "|entry=" + function.getEntryPoint() +
                    "|name=" + function.getName(true) +
                    "|completed=" + results.decompileCompleted()
                );
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
