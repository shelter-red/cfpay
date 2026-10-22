// Decompiles the indirect dispatcher that invokes the structured-record append callback.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.Map;

public class DecompileWechatRecordAppendDispatcher extends GhidraScript {
    private static final String CALL_SITE = "18000d23c";
    private static final int MAX_DEPTH = 2;

    private record Pending(Function function, int depth) {}

    @Override
    protected void run() throws Exception {
        Address callSite = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(CALL_SITE);
        Function root = currentProgram.getListing().getFunctionContaining(callSite);
        if (root == null) throw new IllegalStateException("Dispatcher function was not found");

        ArrayDeque<Pending> queue = new ArrayDeque<>();
        Map<Address, Pending> selected = new LinkedHashMap<>();
        queue.addLast(new Pending(root, 0));
        while (!queue.isEmpty() && !monitor.isCancelled()) {
            Pending item = queue.removeFirst();
            Address entry = item.function().getEntryPoint();
            if (selected.containsKey(entry)) continue;
            selected.put(entry, item);
            println("SELECTED|entry=" + entry + "|depth=" + item.depth());
            if (item.depth() >= MAX_DEPTH) continue;
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(entry);
            while (references.hasNext()) {
                Reference reference = references.next();
                if (!reference.getReferenceType().isCall() && !reference.getReferenceType().isJump()) continue;
                Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                if (caller == null || caller.equals(item.function())) continue;
                println("CALLER|from=" + caller.getEntryPoint() + "|site=" + reference.getFromAddress() +
                    "|to=" + entry + "|type=" + reference.getReferenceType());
                queue.addLast(new Pending(caller, item.depth() + 1));
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Pending item : selected.values()) {
                Function function = item.function();
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|depth=" + item.depth() +
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
