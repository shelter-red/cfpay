// Finds callback-table registrations for the live structured-record batch consumer.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Data;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.LinkedHashMap;
import java.util.Map;

public class TraceWechatPostDecodeCallbackRegistration extends GhidraScript {
    private static final String TARGET = "181974590";
    private static final String CONSTRUCTOR = "181946cd0";
    private static final String[] DATA_ANCHORS = {"189107be8", "189107648"};

    @Override
    protected void run() throws Exception {
        Address target = toAddr(TARGET);
        printReferences("ENTRY_REFERENCE", target);
        Address constructor = toAddr(CONSTRUCTOR);
        ReferenceIterator constructorReferences = currentProgram.getReferenceManager().getReferencesTo(constructor);
        Map<Address, Function> callers = new LinkedHashMap<>();
        while (constructorReferences.hasNext()) {
            Reference reference = constructorReferences.next();
            Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println("CONSTRUCTOR_REFERENCE|from=" + reference.getFromAddress() + "|type=" +
                reference.getReferenceType() + "|function=" + describe(caller));
            if (caller != null) callers.putIfAbsent(caller.getEntryPoint(), caller);
        }
        for (String anchor : DATA_ANCHORS) {
            Address address = toAddr(anchor);
            Data data = currentProgram.getListing().getDataContaining(address);
            println("DATA_ANCHOR|address=" + address + "|data=" + (data == null ? "<none>" : data.toString()) +
                "|value=" + (data == null ? "<none>" : String.valueOf(data.getValue())));
        }

        byte[] pointer = ByteBuffer.allocate(Long.BYTES)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putLong(target.getOffset())
            .array();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized()) continue;
            Address cursor = block.getStart();
            while (cursor != null && cursor.compareTo(block.getEnd()) <= 0 && !monitor.isCancelled()) {
                Address match = currentProgram.getMemory().findBytes(
                    cursor, block.getEnd(), pointer, null, true, monitor);
                if (match == null) break;
                println("POINTER|address=" + match + "|block=" + block.getName());
                for (long delta = -0x80; delta <= 0x80; delta += Long.BYTES) {
                    Address slot = match.add(delta);
                    long value = currentProgram.getMemory().getLong(slot);
                    Address valueAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                    Function valueFunction = currentProgram.getFunctionManager().getFunctionAt(valueAddress);
                    println("SLOT|address=" + slot + "|delta=" + delta + "|value=" + valueAddress +
                        "|function=" + describe(valueFunction));
                    ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(slot);
                    while (references.hasNext()) {
                        Reference reference = references.next();
                        Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                        println("POINTER_REFERENCE|slot=" + slot + "|from=" + reference.getFromAddress() + "|type=" +
                            reference.getReferenceType() + "|function=" + describe(caller));
                        if (caller != null) callers.putIfAbsent(caller.getEntryPoint(), caller);
                    }
                }
                cursor = match.next();
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function caller : callers.values()) {
                DecompileResults results = decompiler.decompileFunction(caller, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + caller.getEntryPoint() + "|name=" + caller.getName() +
                    "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + caller.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
        println("SUMMARY|callers=" + callers.size() + "|cancelled=" + monitor.isCancelled());
    }

    private void printReferences(String label, Address address) {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println(label + "|from=" + reference.getFromAddress() + "|type=" + reference.getReferenceType() +
                "|function=" + describe(caller));
        }
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
