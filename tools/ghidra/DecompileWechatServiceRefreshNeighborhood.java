// Decompiles the service-session refresh neighborhood to locate decoded-record writers.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DecompileWechatServiceRefreshNeighborhood extends GhidraScript {
    private static final String START = "1848ff000";
    private static final String END = "184906000";

    @Override
    protected void run() throws Exception {
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(START);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(END);
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(start, true);
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int count = 0;
        try {
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                if (function.getEntryPoint().compareTo(end) >= 0) break;
                count += 1;
                println("NEIGHBOR|entry=" + function.getEntryPoint() + "|size=" +
                    function.getBody().getNumAddresses());
                ReferenceIterator references = currentProgram.getReferenceManager()
                    .getReferencesTo(function.getEntryPoint());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                    println("IN_REF|target=" + function.getEntryPoint() + "|from=" +
                        reference.getFromAddress() + "|type=" + reference.getReferenceType() + "|owner=" +
                        (owner == null ? "<none>" : owner.getEntryPoint()));
                }
                DecompileResults results = decompiler.decompileFunction(function, 150, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|completed=" +
                    results.decompileCompleted());
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
        println("SUMMARY|count=" + count + "|cancelled=" + monitor.isCancelled());
    }
}
