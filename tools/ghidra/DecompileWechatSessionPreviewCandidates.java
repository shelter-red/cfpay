// Decompiles global session-list summary and unread-preview candidates.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.List;

public class DecompileWechatSessionPreviewCandidates extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "18017beb0",
        "180891150",
        "180891910",
        "180891eb0",
        "180892320",
        "1808927f0",
        "1808927d0",
        "180892500",
        "180892840",
        "1809ac0f0",
        "1809ac120",
        "1809ac1c0",
        "1809ac210",
        "1809ac3e0",
        "1809acd20",
        "1809ab450",
        "1809ab500",
        "1809ab580",
        "1809ad250",
        "1809ad2f0",
        "181c43510",
        "181c43e20",
        "181c43ef0",
        "181c4bfb0",
        "181c4bfe0",
        "1839980e0",
        "18452e710",
        "18452f880",
        "1861d2740",
        "1861d5ca0",
        "186cefae0",
        "186f5ffd0",
        "186f60a30"
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
