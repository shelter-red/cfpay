// Traces the service-session delivery dispatcher between batch begin and batch completion.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class TraceWechatDecodedServiceDelivery extends GhidraScript {
    private static final int MAX_DEPTH = 2;
    private static final int MAX_FUNCTIONS = 28;
    private static final List<String> SEEDS = List.of(
        "185f1edb0",
        "185f20d60",
        "185f20d90",
        "185f26070",
        "185f26bf0",
        "185f26dd0"
    );

    private record Pending(Function function, int depth, String reason) {}

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        ArrayDeque<Pending> queue = new ArrayDeque<>();
        Map<Address, Pending> selected = new LinkedHashMap<>();
        for (String seedText : SEEDS) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(seedText);
            Function function = listing.getFunctionAt(address);
            if (function == null) function = listing.getFunctionContaining(address);
            if (function == null) {
                println("SEED_MISSING|address=" + seedText);
            } else {
                queue.addLast(new Pending(function, 0, "seed:" + seedText));
            }
        }
        while (!queue.isEmpty() && selected.size() < MAX_FUNCTIONS && !monitor.isCancelled()) {
            Pending item = queue.removeFirst();
            Address entry = item.function().getEntryPoint();
            if (selected.containsKey(entry)) continue;
            selected.put(entry, item);
            println("SELECTED|entry=" + entry + "|depth=" + item.depth() + "|reason=" + item.reason());
            if (item.depth() >= MAX_DEPTH) continue;
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(entry);
            while (references.hasNext() && selected.size() + queue.size() < MAX_FUNCTIONS) {
                Reference reference = references.next();
                if (!reference.getReferenceType().isCall() && !reference.getReferenceType().isJump()) continue;
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                if (caller == null || caller.equals(item.function())) continue;
                println("CALLER_EDGE|from=" + caller.getEntryPoint() + "|site=" +
                    reference.getFromAddress() + "|to=" + entry + "|type=" + reference.getReferenceType());
                queue.addLast(new Pending(caller, item.depth() + 1, "caller-of:" + entry));
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
        println("SUMMARY|selected=" + selected.size() + "|cancelled=" + monitor.isCancelled());
    }
}
