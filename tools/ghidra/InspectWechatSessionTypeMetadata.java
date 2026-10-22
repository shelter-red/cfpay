// Inspects stored pointers and neighboring entries for session-related type metadata.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class InspectWechatSessionTypeMetadata extends GhidraScript {
    private static final Map<String, String> TARGETS = new LinkedHashMap<>();

    static {
        TARGETS.put("ChatSessionCell.class", "18b13e518");
        TARGETS.put("ChatSessionCell.cast", "180891910");
        TARGETS.put("ChatSessionCellViewModel.class", "18b13ebf8");
        TARGETS.put("ChatSessionCellViewModel.cast", "1808927f0");
        TARGETS.put("ChatSessionCellViewModel.vtableA", "189187238");
        TARGETS.put("ChatSessionCellViewModel.vtableB", "189a03dd8");
        TARGETS.put("ServiceSessionCell.class", "18b13e8c8");
        TARGETS.put("ServiceSessionCell.cast", "180892320");
    }

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        for (Map.Entry<String, String> target : TARGETS.entrySet()) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target.getValue());
            println("TARGET_BEGIN|name=" + target.getKey() + "|address=" + address);

            ReferenceIterator known = currentProgram.getReferenceManager().getReferencesTo(address);
            int knownCount = 0;
            while (known.hasNext()) {
                Reference reference = known.next();
                Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("KNOWN_REF|name=" + target.getKey() + "|from=" + reference.getFromAddress() +
                    "|type=" + reference.getReferenceType() + "|caller=" +
                    (caller == null ? "<none>" : caller.getName(true)) + "|caller_entry=" +
                    (caller == null ? "<none>" : caller.getEntryPoint()));
                knownCount += 1;
            }

            byte[] pointer = littleEndian64(address.getOffset());
            Address cursor = currentProgram.getMinAddress();
            int pointerCount = 0;
            while (cursor != null && !monitor.isCancelled()) {
                Address found = memory.findBytes(cursor, pointer, null, true, monitor);
                if (found == null) break;
                MemoryBlock block = memory.getBlock(found);
                println("POINTER|name=" + target.getKey() + "|address=" + found + "|block=" +
                    (block == null ? "<none>" : block.getName()));
                inspectNeighborhood(target.getKey(), found);
                pointerCount += 1;
                if (found.equals(currentProgram.getMaxAddress())) break;
                cursor = found.add(1);
            }
            println("TARGET_END|name=" + target.getKey() + "|known=" + knownCount + "|pointers=" + pointerCount);
        }
    }

    private void inspectNeighborhood(String name, Address pointerAddress) throws Exception {
        for (int offset = -0x40; offset <= 0x80; offset += 8) {
            Address slot;
            try {
                slot = pointerAddress.add(offset);
            } catch (Exception exception) {
                continue;
            }
            MemoryBlock slotBlock = currentProgram.getMemory().getBlock(slot);
            if (slotBlock == null || !slotBlock.isInitialized()) continue;
            long raw = currentProgram.getMemory().getLong(slot);
            Address value = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(raw);
            MemoryBlock valueBlock = currentProgram.getMemory().getBlock(value);
            Function function = null;
            if (valueBlock != null && valueBlock.isExecute()) {
                function = currentProgram.getListing().getFunctionAt(value);
                if (function == null) function = currentProgram.getListing().getFunctionContaining(value);
            }
            println("NEIGHBOR|name=" + name + "|base=" + pointerAddress + "|offset=" + offset +
                "|slot=" + slot + "|raw=0x" + Long.toHexString(raw) + "|target_block=" +
                (valueBlock == null ? "<none>" : valueBlock.getName()) + "|function=" +
                (function == null ? "<none>" : function.getName(true)) + "|entry=" +
                (function == null ? "<none>" : function.getEntryPoint()));
        }
    }

    private byte[] littleEndian64(long value) {
        byte[] result = new byte[8];
        for (int index = 0; index < result.length; index++) {
            result[index] = (byte) (value >>> (index * 8));
        }
        return result;
    }
}
