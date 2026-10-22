import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class FindWechatLoginQrReferences extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "188fb0df8",
        "188fb0fa8",
        "188fb10c8",
        "188fb1158",
        "188fb11e8",
        "188fb1288",
        "188fb1318",
        "188fb13a8",
        "188d88c48",
        "1891f90f0"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        ReferenceManager references = currentProgram.getReferenceManager();
        Set<String> functions = new HashSet<>();

        for (String value : TARGETS) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            println("TARGET|address=" + target + "|ascii=" + clean(readAscii(memory, target)));
            ReferenceIterator direct = references.getReferencesTo(target);
            while (direct.hasNext()) {
                Reference reference = direct.next();
                reportReference("DIRECT", reference.getFromAddress(), target, listing, memory, functions);
            }
            scanPointers(memory, target, listing, references, functions);
        }

        decompileFunctions(functions, listing);
    }

    private void scanPointers(
        Memory memory,
        Address target,
        Listing listing,
        ReferenceManager references,
        Set<String> functions
    ) throws Exception {
        byte[] pointer = littleEndian(target.getOffset());
        Address cursor = currentProgram.getMinAddress();
        int count = 0;
        while (cursor != null && !monitor.isCancelled()) {
            Address found = memory.findBytes(cursor, pointer, null, true, monitor);
            if (found == null) break;
            MemoryBlock block = memory.getBlock(found);
            println("POINTER|target=" + target + "|address=" + found + "|block=" +
                (block == null ? "<none>" : block.getName()));
            ReferenceIterator incoming = references.getReferencesTo(found);
            while (incoming.hasNext()) {
                Reference reference = incoming.next();
                reportReference("POINTER_REF", reference.getFromAddress(), found, listing, memory, functions);
            }
            count += 1;
            cursor = found.equals(currentProgram.getMaxAddress()) ? null : found.add(1);
        }
        println("POINTER_SUMMARY|target=" + target + "|pointers=" + count);
    }

    private void reportReference(
        String kind,
        Address from,
        Address target,
        Listing listing,
        Memory memory,
        Set<String> functions
    ) {
        Function function = listing.getFunctionContaining(from);
        MemoryBlock block = memory.getBlock(from);
        String entry = function == null ? "<none>" : function.getEntryPoint().toString();
        if (function != null) functions.add(entry);
        println(kind + "|from=" + from + "|target=" + target + "|block=" +
            (block == null ? "<none>" : block.getName()) + "|function=" +
            (function == null ? "<none>" : function.getName(true)) + "|entry=" + entry);
    }

    private void decompileFunctions(Set<String> entries, Listing listing) throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException("decompiler unavailable");
        try {
            for (String entry : new ArrayList<>(entries)) {
                Function function = listing.getFunctionAt(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(entry));
                if (function == null) continue;
                DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
                println("FUNCTION_BEGIN|entry=" + entry + "|name=" + function.getName(true) +
                    "|completed=" + result.decompileCompleted());
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) println(result.getDecompiledFunction().getC());
                println("FUNCTION_END|entry=" + entry);
            }
        } finally {
            decompiler.dispose();
        }
    }

    private String readAscii(Memory memory, Address address) {
        StringBuilder value = new StringBuilder();
        try {
            for (int index = 0; index < 256; index += 1) {
                int character = memory.getByte(address.add(index)) & 0xff;
                if (character == 0) break;
                if (character < 0x20 || character > 0x7e) return "";
                value.append((char) character);
            }
        } catch (Exception ignored) {
            return "";
        }
        return value.toString();
    }

    private byte[] littleEndian(long value) {
        byte[] result = new byte[8];
        for (int index = 0; index < result.length; index += 1) result[index] = (byte) (value >>> (index * 8));
        return result;
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
