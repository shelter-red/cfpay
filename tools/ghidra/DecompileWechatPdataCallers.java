// Maps selected string references to PE x64 runtime-function entries and decompiles their callers.
// @category CFPay

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.SourceType;

import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

public class DecompileWechatPdataCallers extends GhidraScript {
    private static final String[] ANCHORS = {
        "/cgi-bin/micromsg-bin/newsync",
        "micromsg.NewSyncResponse",
        "paymsg",
        "PayMsgType",
        "wcpay_info"
    };

    private final Map<Long, FunctionRange> ranges = new LinkedHashMap<>();

    @Override
    protected void run() throws Exception {
        loadRuntimeFunctionTable();
        Listing listing = currentProgram.getListing();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        Map<Long, Candidate> candidates = new LinkedHashMap<>();
        try {
            for (String anchor : ANCHORS) {
                Address target = findString(anchor);
                if (target == null) {
                    println("ANCHOR_MISSING|text=" + clean(anchor));
                    continue;
                }
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
                while (references.hasNext()) {
                    Reference reference = references.next();
                    FunctionRange range = findContainingRange(reference.getFromAddress());
                    if (range == null) {
                        println("RANGE_MISSING|text=" + clean(anchor) + "|from=" + reference.getFromAddress());
                        continue;
                    }
                    candidates.putIfAbsent(range.start.getOffset(), new Candidate(anchor, reference.getFromAddress(), range));
                }
            }

            for (Candidate candidate : candidates.values()) {
                if (monitor.isCancelled()) break;
                AddressSet body = new AddressSet(candidate.range.start, candidate.range.end.subtract(1));
                DisassembleCommand command = new DisassembleCommand(candidate.range.start, body, true);
                command.enableCodeAnalysis(false);
                boolean disassembled = command.applyTo(currentProgram, monitor);
                Function function = listing.getFunctionAt(candidate.range.start);
                if (function == null) {
                    function = listing.createFunction(
                        "pdata_caller_" + candidate.range.start.toString(),
                        candidate.range.start,
                        body,
                        SourceType.USER_DEFINED);
                }
                println("FUNCTION_BEGIN|anchor=" + clean(candidate.anchor) + "|reference=" + candidate.reference +
                    "|entry=" + candidate.range.start + "|end=" + candidate.range.end +
                    "|disassembled=" + disassembled);
                DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
                println("DECOMPILE|completed=" + result.decompileCompleted());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    println(result.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + clean(result.getErrorMessage()));
                }
                println("FUNCTION_END|entry=" + candidate.range.start);
            }
            println("SUMMARY|pdata_entries=" + ranges.size() + "|functions=" + candidates.size());
        } finally {
            decompiler.dispose();
        }
    }

    private void loadRuntimeFunctionTable() throws Exception {
        MemoryBlock block = currentProgram.getMemory().getBlock(".pdata");
        if (block == null) throw new IllegalStateException("PE .pdata section not found");
        byte[] entry = new byte[12];
        for (long offset = 0; offset + entry.length <= block.getSize(); offset += entry.length) {
            Address location = block.getStart().add(offset);
            currentProgram.getMemory().getBytes(location, entry);
            long startRva = unsignedInt(entry, 0);
            long endRva = unsignedInt(entry, 4);
            if (startRva == 0 || endRva <= startRva) continue;
            Address start = currentProgram.getImageBase().add(startRva);
            Address end = currentProgram.getImageBase().add(endRva);
            ranges.put(start.getOffset(), new FunctionRange(start, end));
        }
    }

    private FunctionRange findContainingRange(Address address) {
        long imageRva = address.subtract(currentProgram.getImageBase());
        for (FunctionRange range : ranges.values()) {
            long startRva = range.start.subtract(currentProgram.getImageBase());
            long endRva = range.end.subtract(currentProgram.getImageBase());
            if (imageRva >= startRva && imageRva < endRva) return range;
        }
        return null;
    }

    private Address findString(String value) throws Exception {
        byte[] bytes = value.getBytes(StandardCharsets.US_ASCII);
        Address cursor = currentProgram.getMinAddress();
        while (cursor != null && !monitor.isCancelled()) {
            Address match = currentProgram.getMemory().findBytes(cursor, bytes, null, true, monitor);
            if (match == null) return null;
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(match);
            if (references.hasNext()) return match;
            cursor = match.add(1);
        }
        return null;
    }

    private long unsignedInt(byte[] bytes, int offset) {
        return (bytes[offset] & 0xffL) | ((bytes[offset + 1] & 0xffL) << 8) |
            ((bytes[offset + 2] & 0xffL) << 16) | ((bytes[offset + 3] & 0xffL) << 24);
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }

    private static final class FunctionRange {
        private final Address start;
        private final Address end;

        private FunctionRange(Address start, Address end) {
            this.start = start;
            this.end = end;
        }
    }

    private static final class Candidate {
        private final String anchor;
        private final Address reference;
        private final FunctionRange range;

        private Candidate(String anchor, Address reference, FunctionRange range) {
            this.anchor = anchor;
            this.reference = reference;
            this.range = range;
        }
    }
}
