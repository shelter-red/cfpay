// Traces the callback-table slot that appends decoded 0x300-byte service-session records.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class TraceWechatVectorAppendRegistration extends GhidraScript {
    private static final String SLOT = "18bd3d668";
    private static final int FIRST = -24;
    private static final int LAST = 24;

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Address slot = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(SLOT);
        Set<Function> selected = new LinkedHashSet<>();
        for (int index = FIRST; index <= LAST; index += 1) {
            Address address = slot.add(index * 4L);
            int rawValue;
            try {
                rawValue = memory.getInt(address);
            } catch (Exception exception) {
                println("SLOT_ERROR|index=" + index + "|address=" + address);
                continue;
            }
            long unsignedRva = Integer.toUnsignedLong(rawValue);
            Address value = currentProgram.getImageBase().add(unsignedRva);
            Function target = currentProgram.getListing().getFunctionAt(value);
            println("SLOT|index=" + index + "|address=" + address + "|rva=" +
                Long.toHexString(unsignedRva) + "|value=" + value + "|target=" +
                (target == null ? "<none>" : target.getName(true)));
            if (target != null) selected.add(target);

            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
            while (references.hasNext()) {
                Reference reference = references.next();
                Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("SLOT_REF|slot=" + address + "|from=" + reference.getFromAddress() + "|type=" +
                    reference.getReferenceType() + "|owner=" +
                    (owner == null ? "<none>" : owner.getEntryPoint()));
                if (owner != null) selected.add(owner);
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        try {
            for (Function function : selected) {
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
        println("SUMMARY|functions=" + selected.size() + "|cancelled=" + monitor.isCancelled());
    }
}
