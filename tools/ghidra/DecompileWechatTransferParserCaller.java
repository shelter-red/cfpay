// Decompiles the sole direct caller of the Weixin transfer message parser.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileWechatTransferParserCaller extends GhidraScript {
    private static final String ENTRY = "1853c9760";

    @Override
    protected void run() throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ENTRY);
        Function function = currentProgram.getListing().getFunctionAt(address);
        if (function == null) function = currentProgram.getListing().getFunctionContaining(address);
        if (function == null) {
            println("FUNCTION_MISSING|requested=" + ENTRY);
            return;
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
            println(
                "FUNCTION_BEGIN|requested=" + ENTRY+
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
        } finally {
            decompiler.dispose();
        }
    }
}
