// Decompiles login UI and QR payload candidates for the pinned Weixin.dll.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatQrCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "18007ed20",
        "180962f00",
        "181fcc9e0",
        "181aabb90",
        "1822e7eb0",
        "1822e9500",
        "18b149000"
    );

    @Override
    protected void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("Unable to open current program");
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
                DecompileResults result = decompiler.decompileFunction(function, 90, monitor);
                println("FUNCTION_BEGIN|requested=" + entry + "|entry=" + function.getEntryPoint() +
                    "|name=" + function.getName(true) + "|completed=" + result.decompileCompleted());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    println(result.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + result.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
