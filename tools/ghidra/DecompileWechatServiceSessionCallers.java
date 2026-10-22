// Decompiles direct callers of dynamically confirmed Weixin service-session candidates.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatServiceSessionCallers extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "182203e40",
        "184051b10",
        "1848f0cb0",
        "1848f6b60",
        "1848fd550",
        "184900a00",
        "1849026f0",
        "184904710",
        "184909dd0",
        "18490a800",
        "18490b260",
        "18490b5c0",
        "18490b920",
        "18490bbc0",
        "18490fcb0",
        "185f25500",
        "185f288c0"
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
