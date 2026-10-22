// Traces NewSync network and protobuf anchors to response parsing functions.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import java.util.Map;

public class TraceWechatNewSyncNetworkPath extends GhidraScript {
    private static final String[] ANCHORS = {
        "/cgi-bin/micromsg-bin/newsync",
        "new_sync.cc",
        "micromsg.NewSyncResponse",
        "NewSyncStart",
        "NewSyncEnd",
    };
    private static final int MAX_DATA_DEPTH = 4;

    private record Pending(String anchor, Address address, int depth) {}

    @Override
    protected void run() throws Exception {
        ArrayDeque<Pending> queue = new ArrayDeque<>();
        Map<Address, Function> functions = new LinkedHashMap<>();
        Map<String, Boolean> visited = new LinkedHashMap<>();
        for (String anchor : ANCHORS) findAnchors(anchor, queue);

        while (!queue.isEmpty() && !monitor.isCancelled()) {
            Pending pending = queue.removeFirst();
            String key = pending.anchor() + "@" + pending.address();
            if (visited.putIfAbsent(key, true) != null) continue;
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(pending.address());
            while (references.hasNext()) {
                Reference reference = references.next();
                Function function = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("REFERENCE|anchor=" + pending.anchor() + "|depth=" + pending.depth() + "|target=" +
                    pending.address() + "|from=" + reference.getFromAddress() + "|type=" +
                    reference.getReferenceType() + "|function=" + describe(function));
                if (function != null) {
                    functions.putIfAbsent(function.getEntryPoint(), function);
                } else if (pending.depth() < MAX_DATA_DEPTH) {
                    queue.addLast(new Pending(pending.anchor(), reference.getFromAddress(), pending.depth() + 1));
                }
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function function : functions.values()) {
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
        println("SUMMARY|visited=" + visited.size() + "|functions=" + functions.size());
    }

    private void findAnchors(String text, ArrayDeque<Pending> queue) throws Exception {
        byte[] bytes = text.getBytes(StandardCharsets.US_ASCII);
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized()) continue;
            Address cursor = block.getStart();
            while (cursor != null && cursor.compareTo(block.getEnd()) <= 0 && !monitor.isCancelled()) {
                Address match = currentProgram.getMemory().findBytes(cursor, block.getEnd(), bytes, null, true, monitor);
                if (match == null) break;
                println("ANCHOR|text=" + text + "|address=" + match + "|block=" + block.getName());
                queue.addLast(new Pending(text, match, 0));
                cursor = match.next();
            }
        }
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
