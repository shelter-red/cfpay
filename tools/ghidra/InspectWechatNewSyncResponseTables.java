// Separates the adjacent NewSync request/response protobuf tables.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class InspectWechatNewSyncResponseTables extends GhidraScript {
    private record Table(String name, String address, int firstSlot, int lastSlot, int elementBytes,
                         boolean imageRelative, boolean decompileTargets) {}

    private static final Table[] TABLES = {
        new Table("NewSyncRequest.vtable", "188fc0cc8", -4, 11, 8, false, false),
        new Table("NewSyncResponse.vtable", "188fc0d48", -4, 11, 8, false, true),
        new Table("NewSync.generated", "18ba58df0", 60, 160, 4, true, false),
    };

    @Override
    protected void run() throws Exception {
        printSignature("NewSyncRequest.parser", "18106eb90");
        printSignature("NewSyncResponse.parser", "18106fb10");
        Memory memory = currentProgram.getMemory();
        Map<Address, Function> functions = new LinkedHashMap<>();
        for (Table item : TABLES) {
            Address table = toAddr(item.address());
            println("TABLE_BEGIN|name=" + item.name() + "|address=" + table);
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(table);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("TABLE_REF|name=" + item.name() + "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() + "|owner=" + describe(owner));
            }
            for (int slot = item.firstSlot(); slot <= item.lastSlot(); slot += 1) {
                Address address = table.add(slot * (long)item.elementBytes());
                long value;
                try {
                    value = item.elementBytes() == 4
                        ? Integer.toUnsignedLong(memory.getInt(address))
                        : memory.getLong(address);
                } catch (Exception exception) {
                    println("SLOT_ERROR|name=" + item.name() + "|slot=" + slot + "|address=" + address);
                    continue;
                }
                Address targetAddress = item.imageRelative()
                    ? currentProgram.getImageBase().add(value)
                    : toAddr(Long.toUnsignedString(value, 16));
                MemoryBlock block = memory.getBlock(targetAddress);
                Function target = currentProgram.getListing().getFunctionAt(targetAddress);
                if (target == null) target = currentProgram.getListing().getFunctionContaining(targetAddress);
                println("SLOT|name=" + item.name() + "|slot=" + slot + "|address=" + address +
                    "|value=" + targetAddress + "|target=" + describe(target) + "|block=" +
                    (block == null ? "<none>" : block.getName()));
                if (item.decompileTargets() && block != null && block.isExecute() && target != null) {
                    functions.putIfAbsent(target.getEntryPoint(), target);
                }
            }
            println("TABLE_END|name=" + item.name());
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (Function function : functions.values()) {
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName() +
                    "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                    completed += 1;
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
        println("SUMMARY|functions=" + functions.size() + "|completed=" + completed);
    }

    private void printSignature(String name, String requested) throws Exception {
        Address address = toAddr(requested);
        byte[] bytes = new byte[32];
        currentProgram.getMemory().getBytes(address, bytes);
        StringBuilder output = new StringBuilder();
        for (byte value : bytes) {
            if (output.length() > 0) output.append(' ');
            output.append(String.format("%02x", value & 0xff));
        }
        println("SIGNATURE|name=" + name + "|requested=" + requested + "|bytes=" + output);
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
