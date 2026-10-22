// Reports code references and raw function-pointer locations for payment candidates.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.List;

public class FindWechatPaymentCallers extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "185297710",
        "18529ed20",
        "1852a2c80",
        "18529a850",
        "1852a2110",
        "1852a58d0"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();

        for (String value : ENTRIES) {
            Address entry = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            Function target = listing.getFunctionAt(entry);
            println("TARGET|entry=" + entry + "|name=" + (target == null ? "<none>" : target.getName(true)));

            int references = 0;
            ReferenceIterator iterator = referenceManager.getReferencesTo(entry);
            while (iterator.hasNext()) {
                Reference reference = iterator.next();
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                MemoryBlock block = memory.getBlock(reference.getFromAddress());
                println(
                    "REF|target=" + entry +
                    "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() +
                    "|block=" + (block == null ? "<none>" : block.getName()) +
                    "|caller=" + (caller == null ? "<none>" : caller.getName(true)) +
                    "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
                );
                references += 1;
            }

            byte[] pointer = littleEndian(entry.getOffset());
            Address cursor = currentProgram.getMinAddress();
            int pointers = 0;
            while (cursor != null && !monitor.isCancelled()) {
                Address found = memory.findBytes(cursor, pointer, null, true, monitor);
                if (found == null) break;
                MemoryBlock block = memory.getBlock(found);
                println(
                    "POINTER|target=" + entry+
                    "|address=" + found+
                    "|block=" + (block == null ? "<none>" : block.getName())+
                    "|execute=" + (block != null && block.isExecute())
                );
                pointers += 1;
                if (found.equals(currentProgram.getMaxAddress())) break;
                cursor = found.add(1);
            }
            println("TARGET_SUMMARY|entry=" + entry + "|references=" + references + "|pointers=" + pointers);
        }
    }

    private byte[] littleEndian(long value) {
        byte[] result = new byte[8];
        for (int index = 0; index < result.length; index++) {
            result[index] = (byte) (value >>> (index * 8));
        }
        return result;
    }
}
