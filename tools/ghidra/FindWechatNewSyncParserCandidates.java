// Finds 4.2 functions matching the 4.1 NewSyncResponse parser prologue and compares their bodies.
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
import ghidra.program.model.symbol.SourceType;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

public class FindWechatNewSyncParserCandidates extends GhidraScript {
    private static final byte[] SIGNATURE = hex(
        "5541574156415541545657534883ec48488d6c244048c74500feffffff4889d6");
    private static final long MIN_FUNCTION_SIZE = 0x400;

    @Override
    protected void run() throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        MemoryBlock pdata = currentProgram.getMemory().getBlock(".pdata");
        if (text == null || pdata == null) throw new IllegalStateException("PE .text/.pdata section missing");

        List<FunctionRange> ranges = readFunctionRanges(pdata);
        List<FunctionRange> candidates = findCandidates(text, ranges);
        candidates.sort(Comparator.comparingLong((FunctionRange range) -> range.end.subtract(range.start)).reversed());
        println("SUMMARY|matching_entries=" + candidates.size() + "|minimum_size=0x" +
            Long.toHexString(MIN_FUNCTION_SIZE));

        Listing listing = currentProgram.getListing();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            int index = 0;
            for (FunctionRange range : candidates) {
                if (monitor.isCancelled()) break;
                long size = range.end.subtract(range.start);
                println("CANDIDATE|rva=0x" + Long.toHexString(range.start.subtract(currentProgram.getImageBase())) +
                    "|size=0x" + Long.toHexString(size));
                if (size < MIN_FUNCTION_SIZE) continue;
                AddressSet body = new AddressSet(range.start, range.end.subtract(1));
                DisassembleCommand command = new DisassembleCommand(range.start, body, true);
                command.enableCodeAnalysis(false);
                command.applyTo(currentProgram, monitor);
                Function function = listing.getFunctionAt(range.start);
                if (function == null) {
                    function = listing.createFunction("parser_candidate_" + index, range.start, body,
                        SourceType.USER_DEFINED);
                }
                DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|rva=0x" + Long.toHexString(range.start.subtract(currentProgram.getImageBase())) +
                    "|size=0x" + Long.toHexString(size) + "|completed=" + result.decompileCompleted());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    println(result.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + clean(result.getErrorMessage()));
                }
                println("FUNCTION_END|rva=0x" + Long.toHexString(range.start.subtract(currentProgram.getImageBase())) + "|");
                index += 1;
            }
        } finally {
            decompiler.dispose();
        }
    }

    private List<FunctionRange> readFunctionRanges(MemoryBlock pdata) throws Exception {
        byte[] bytes = new byte[(int)pdata.getSize()];
        currentProgram.getMemory().getBytes(pdata.getStart(), bytes);
        List<FunctionRange> ranges = new ArrayList<>();
        for (int offset = 0; offset + 12 <= bytes.length; offset += 12) {
            long startRva = uint32(bytes, offset);
            long endRva = uint32(bytes, offset + 4);
            if (startRva == 0 || endRva <= startRva) continue;
            ranges.add(new FunctionRange(currentProgram.getImageBase().add(startRva),
                currentProgram.getImageBase().add(endRva)));
        }
        return ranges;
    }

    private List<FunctionRange> findCandidates(MemoryBlock text, List<FunctionRange> ranges) throws Exception {
        byte[] bytes = new byte[(int)text.getSize()];
        currentProgram.getMemory().getBytes(text.getStart(), bytes);
        List<FunctionRange> candidates = new ArrayList<>();
        for (int offset = 0; offset <= bytes.length - SIGNATURE.length; offset += 1) {
            if (!matches(bytes, offset, SIGNATURE)) continue;
            Address hit = text.getStart().add(offset);
            FunctionRange range = findContainingRange(hit, ranges);
            if (range != null && range.start.equals(hit)) candidates.add(range);
        }
        return candidates;
    }

    private FunctionRange findContainingRange(Address address, List<FunctionRange> ranges) {
        long rva = address.subtract(currentProgram.getImageBase());
        int low = 0;
        int high = ranges.size() - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            FunctionRange range = ranges.get(middle);
            long start = range.start.subtract(currentProgram.getImageBase());
            long end = range.end.subtract(currentProgram.getImageBase());
            if (rva < start) high = middle - 1;
            else if (rva >= end) low = middle + 1;
            else return range;
        }
        return null;
    }

    private boolean matches(byte[] input, int offset, byte[] pattern) {
        for (int index = 0; index < pattern.length; index += 1) {
            if (input[offset + index] != pattern[index]) return false;
        }
        return true;
    }

    private long uint32(byte[] bytes, int offset) {
        return (bytes[offset] & 0xffL) | ((bytes[offset + 1] & 0xffL) << 8) |
            ((bytes[offset + 2] & 0xffL) << 16) | ((bytes[offset + 3] & 0xffL) << 24);
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }

    private static byte[] hex(String value) {
        byte[] bytes = new byte[value.length() / 2];
        for (int index = 0; index < bytes.length; index += 1) {
            bytes[index] = (byte)Integer.parseInt(value.substring(index * 2, index * 2 + 2), 16);
        }
        return bytes;
    }

    private static final class FunctionRange {
        private final Address start;
        private final Address end;

        private FunctionRange(Address start, Address end) {
            this.start = start;
            this.end = end;
        }
    }
}
