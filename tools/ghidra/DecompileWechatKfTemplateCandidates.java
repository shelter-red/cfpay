// Decompiles customer-service template message candidates.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatKfTemplateCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "180889160",
        "183d3f560",
        "183d40a00",
        "184647980",
        "1846477d0",
        "18464b250",
        "1849de390",
        "181be3e20",
        "185d70910",
        "183cf0b00",
        "183cf0940",
        "183ce8720",
        "183ce8b70",
        "183ce8800",
        "183cf0360",
        "183cf0670",
        "183cf0250",
        "183cf02c0"
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
                Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(entry);
                Function function = currentProgram.getListing().getFunctionAt(address);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(address);
                if (function == null) {
                    println("FUNCTION_MISSING|requested=" + entry);
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|requested=" + entry + "|entry=" + function.getEntryPoint() + "|name=" +
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
