// Traces callers upstream from receipt-summary model binding and subscription update anchors.
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

public class TraceWechatReceiptSummaryUpstream extends GhidraScript {
    private static final int MAX_DEPTH = 2;
    private static final int MAX_FUNCTIONS = 80;
    private static final List<String> SEEDS = List.of(
        "181c3d540",
        "1849006f0",
        "181c43510",
        "1861d2740",
        "1861d5ca0",
        "186f5ffd0",
        "186f60a30"
    );

    private record Pending(Function function, int depth, String reason) {}

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        ArrayDeque<Pending> pending = new ArrayDeque<>();
        Map<Address, Pending> selected = new LinkedHashMap<>();

        for (String seedText : SEEDS) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(seedText);
            Function function = listing.getFunctionAt(address);
            if (function == null) function = listing.getFunctionContaining(address);
            if (function == null) {
                println("SEED_MISSING|address=" + seedText);
                continue;
            }
            pending.add(new Pending(function, 0, "seed:" + seedText));
        }

        while (!pending.isEmpty() && selected.size() < MAX_FUNCTIONS && !monitor.isCancelled()) {
            Pending item = pending.removeFirst();
            Address entry = item.function().getEntryPoint();
            if (selected.containsKey(entry)) continue;
            selected.put(entry, item);
            println("SELECTED|entry=" + entry + "|name=" + item.function().getName(true) + "|depth=" +
                item.depth() + "|reason=" + item.reason());
            if (item.depth() >= MAX_DEPTH) continue;

            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(entry);
            while (references.hasNext() && selected.size() + pending.size() < MAX_FUNCTIONS) {
                Reference reference = references.next();
                if (!reference.getReferenceType().isCall() && !reference.getReferenceType().isJump()) continue;
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                if (caller == null || caller.equals(item.function())) continue;
                println("CALLER_EDGE|from=" + caller.getEntryPoint() + "|site=" + reference.getFromAddress() +
                    "|to=" + entry + "|type=" + reference.getReferenceType());
                pending.addLast(new Pending(caller, item.depth() + 1, "caller-of:" + entry));
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
                if (monitor.isCancelled()) break;
                Function function = item.function();
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" +
                    function.getName(true) + "|depth=" + item.depth() + "|completed=" +
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

        println("SUMMARY|selected=" + selected.size() + "|max_depth=" + MAX_DEPTH +
            "|cancelled=" + monitor.isCancelled());
    }
}
