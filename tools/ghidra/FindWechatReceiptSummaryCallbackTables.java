// Finds function-table entries and registration references for receipt-summary refresh callbacks.
// @category CFPay

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

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class FindWechatReceiptSummaryCallbackTables extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "1848f7090",
        "184903110",
        "1849032c0",
        "1849006f0"
    );
    private static final int TABLE_RADIUS = 6;

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        Map<Address, Function> registrationFunctions = new LinkedHashMap<>();

        for (String targetText : TARGETS) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(targetText);
            println("TARGET_BEGIN|address=" + target);
            inspectReferences(target, registrationFunctions);

            byte[] pointer = littleEndian(target.getOffset());
            Address cursor = currentProgram.getMinAddress();
            int pointerCount = 0;
            while (cursor != null && !monitor.isCancelled()) {
                Address found = memory.findBytes(cursor, pointer, null, true, monitor);
                if (found == null) break;
                pointerCount += 1;
                MemoryBlock block = memory.getBlock(found);
                println("POINTER|target=" + target + "|address=" + found + "|block=" +
                    (block == null ? "<none>" : block.getName()) + "|write=" +
                    (block != null && block.isWrite()) + "|execute=" + (block != null && block.isExecute()));
                inspectTableNeighborhood(found);
                inspectReferences(found, registrationFunctions);
                if (found.equals(currentProgram.getMaxAddress())) break;
                cursor = found.add(1);
            }
            println("TARGET_END|address=" + target + "|pointers=" + pointerCount);
        }

        decompileRegistrations(registrationFunctions);
        println("SUMMARY|registration_functions=" + registrationFunctions.size() +
            "|cancelled=" + monitor.isCancelled());
    }

    private void inspectReferences(Address target, Map<Address, Function> registrationFunctions) {
        Listing listing = currentProgram.getListing();
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = listing.getFunctionContaining(reference.getFromAddress());
            println("REFERENCE|target=" + target + "|from=" + reference.getFromAddress() + "|type=" +
                reference.getReferenceType() + "|caller=" + (caller == null ? "<none>" : caller.getName(true)) +
                "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint()));
            if (caller != null) registrationFunctions.putIfAbsent(caller.getEntryPoint(), caller);
        }
    }

    private void inspectTableNeighborhood(Address center) {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        for (int slot = -TABLE_RADIUS; slot <= TABLE_RADIUS; slot += 1) {
            Address address;
            try {
                address = center.add(slot * 8L);
            } catch (Exception ignored) {
                continue;
            }
            long value;
            try {
                value = memory.getLong(address);
            } catch (Exception ignored) {
                continue;
            }
            Address candidate = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function function = listing.getFunctionAt(candidate);
            MemoryBlock block = memory.getBlock(candidate);
            println("TABLE_SLOT|center=" + center + "|slot=" + slot + "|address=" + address + "|value=" +
                candidate + "|function=" + (function == null ? "<none>" : function.getName(true)) + "|block=" +
                (block == null ? "<none>" : block.getName()));
        }
    }

    private void decompileRegistrations(Map<Address, Function> functions) {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function function : functions.values()) {
                if (monitor.isCancelled()) break;
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName(true) +
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
    }

    private byte[] littleEndian(long value) {
        byte[] result = new byte[8];
        for (int index = 0; index < result.length; index += 1) {
            result[index] = (byte) (value >>> (index * 8));
        }
        return result;
    }
}
