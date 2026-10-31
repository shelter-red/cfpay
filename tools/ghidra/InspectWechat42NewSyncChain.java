// Decompiles 4.2 NewSyncResponse/CmdItem parser functions and inspects their vtables.
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
import java.util.List;

public class InspectWechat42NewSyncChain extends GhidraScript {
    private static final long[] FUNCTION_RVAS = {0x10b0230, 0x2fcc400, 0x2fcbe10, 0x6a2ef0, 0x1641e70};
    private static final Table[] TABLES = {
        new Table("NewSyncResponse", 0x929f408, 0, 11),
        new Table("CmdList", 0x96d0558, 0, 11),
        new Table("CmdItem", 0x96d04d8, 0, 7)
    };

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            List<FunctionRange> ranges = readFunctionRanges();
            for (long rva : FUNCTION_RVAS) decompileFunction(rva, ranges, listing, decompiler);
            for (Table table : TABLES) printTable(table);
        } finally {
            decompiler.dispose();
        }
    }

    private List<FunctionRange> readFunctionRanges() throws Exception {
        MemoryBlock pdata = currentProgram.getMemory().getBlock(".pdata");
        if (pdata == null) throw new IllegalStateException("PE .pdata section missing");
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

    private void decompileFunction(long rva, List<FunctionRange> ranges, Listing listing,
                                   DecompInterface decompiler) throws Exception {
        Address entry = currentProgram.getImageBase().add(rva);
        FunctionRange range = findContainingRange(entry, ranges);
        if (range == null || !range.start.equals(entry)) {
            println("FUNCTION_RANGE_MISSING|rva=0x" + Long.toHexString(rva));
            return;
        }
        AddressSet body = new AddressSet(range.start, range.end.subtract(1));
        DisassembleCommand command = new DisassembleCommand(range.start, body, true);
        command.enableCodeAnalysis(false);
        command.applyTo(currentProgram, monitor);
        Function function = listing.getFunctionAt(entry);
        if (function == null) {
            function = listing.createFunction("wechat42_rva_" + Long.toHexString(rva), entry, body,
                SourceType.USER_DEFINED);
        }
        DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
        println("FUNCTION_BEGIN|rva=0x" + Long.toHexString(rva) + "|size=0x" +
            Long.toHexString(range.end.subtract(range.start)) + "|completed=" + result.decompileCompleted());
        if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
            println(result.getDecompiledFunction().getC());
        } else {
            println("DECOMPILE_ERROR|" + clean(result.getErrorMessage()));
        }
        println("FUNCTION_END|rva=0x" + Long.toHexString(rva));
    }

    private void printTable(Table table) throws Exception {
        Address base = currentProgram.getImageBase().add(table.rva);
        println("TABLE_BEGIN|name=" + table.name + "|address=" + base);
        for (int slot = table.firstSlot; slot <= table.lastSlot; slot += 1) {
            Address address = base.add(slot * 8L);
            long value = currentProgram.getMemory().getLong(address);
            println("SLOT|name=" + table.name + "|slot=" + slot + "|address=" + address +
                "|value=0x" + Long.toHexString(value));
        }
        println("TABLE_END|name=" + table.name);
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

    private long uint32(byte[] bytes, int offset) {
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

    private static final class Table {
        private final String name;
        private final long rva;
        private final int firstSlot;
        private final int lastSlot;

        private Table(String name, long rva, int firstSlot, int lastSlot) {
            this.name = name;
            this.rva = rva;
            this.firstSlot = firstSlot;
            this.lastSlot = lastSlot;
        }
    }
}
