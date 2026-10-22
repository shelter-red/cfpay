// Decompiles Weixin service-account aggregation candidates found near receipt display strings.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatServiceAccountCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "18037a970",
        "1808ef170",
        "180aabed0",
        "180aae3f0",
        "180b9d6c0",
        "180b9d7f0",
        "180ba02b0",
        "18453dfe0",
        "1848f0e70",
        "1848f7090",
        "1848fbd50",
        "184910560"
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

                DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
                println(
                    "FUNCTION_BEGIN|requested=" + entry+
                    "|entry=" + function.getEntryPoint()+
                    "|name=" + function.getName(true)+
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
