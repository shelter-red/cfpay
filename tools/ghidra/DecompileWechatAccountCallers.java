// Decompiles direct callers of the passive current-account wrapper in Weixin.dll.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatAccountCallers extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "182803b70",
        "1828e8570",
        "182914d30",
        "182914eb0",
        "184c675c0",
        "184c67910",
        "184c68070",
        "184c68280",
        "184c683a0",
        "184c88140",
        "184cd3680",
        "184d26810",
        "184d2d990",
        "184d30790",
        "184d30b90",
        "184d3ed50",
        "184d7cb10",
        "1865d6480",
        "1865dc320",
        "1865e1650",
        "1865e33b0"
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
