// Decompiles the functions that write the service-session record-vector end pointer.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class DecompileWechatVectorWriters extends GhidraScript {
    private static final String[] WRITES = {"184903d27", "18490d61f"};

    @Override
    protected void run() throws Exception {
        Set<Function> selected = new LinkedHashSet<>();
        for (String value : WRITES) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function function = currentProgram.getListing().getFunctionContaining(address);
            println("WRITE|address=" + address + "|function=" +
                (function == null ? "<none>" : function.getEntryPoint()));
            if (function != null) selected.add(function);
        }

        Set<Function> callers = new LinkedHashSet<>();
        for (Function function : selected) {
            ReferenceIterator references = currentProgram.getReferenceManager()
                .getReferencesTo(function.getEntryPoint());
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("IN_REF|target=" + function.getEntryPoint() + "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() + "|owner=" +
                    (owner == null ? "<none>" : owner.getEntryPoint()));
                if (owner != null) callers.add(owner);
            }
        }
        selected.addAll(callers);

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function function : selected) {
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName() +
                    "|completed=" + results.decompileCompleted());
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
        println("SUMMARY|functions=" + selected.size() + "|cancelled=" + monitor.isCancelled());
    }
}
