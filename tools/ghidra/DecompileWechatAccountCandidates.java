// Decompiles passive current-account and profile candidates in Weixin.dll.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatAccountCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "180dc08d0",
        "1800474c0",
        "1800490a0",
        "18004a050",
        "18004b690",
        "18045e2d0",
        "1803b5e90",
        "1803b6560",
        "1803bd230",
        "1806cab50",
        "181c80180",
        "18096a2b0",
        "1809aef50",
        "1800b61a0",
        "1809ae980",
        "1809aec40",
        "181e49140",
        "181e4a620",
        "181e4a700",
        "180020790",
        "1800279b0",
        "181002fd0",
        "180dc09d0",
        "180dc1140",
        "1865e5b60"
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

                DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
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
