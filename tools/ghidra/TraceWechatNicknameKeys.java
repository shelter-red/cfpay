// Traces direct and pointer-table references to Weixin nickname cache keys.
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

import java.nio.charset.StandardCharsets;
import java.util.List;

public class TraceWechatNicknameKeys extends GhidraScript {
    private static final List<String> KEYS = List.of(
        "mmkv_key_nick_name",
        "current_login_nick_name",
        "contact_detail_nick_name"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager references = currentProgram.getReferenceManager();

        for (String key : KEYS) {
            Address cursor = currentProgram.getMinAddress();
            byte[] text = key.getBytes(StandardCharsets.UTF_8);
            while (cursor != null && !monitor.isCancelled()) {
                Address stringAddress = memory.findBytes(cursor, text, null, true, monitor);
                if (stringAddress == null) break;
                println("KEY|name=" + key + "|address=" + stringAddress);
                reportReferences("STRING_REF", stringAddress, listing, memory, references);

                byte[] pointerBytes = littleEndian(stringAddress.getOffset());
                Address pointerCursor = currentProgram.getMinAddress();
                while (pointerCursor != null && !monitor.isCancelled()) {
                    Address pointerAddress = memory.findBytes(pointerCursor, pointerBytes, null, true, monitor);
                    if (pointerAddress == null) break;
                    MemoryBlock block = memory.getBlock(pointerAddress);
                    println("POINTER|name=" + key + "|address=" + pointerAddress + "|block=" +
                        (block == null ? "<none>" : block.getName()));
                    reportReferences("POINTER_REF", pointerAddress, listing, memory, references);
                    if (pointerAddress.equals(currentProgram.getMaxAddress())) break;
                    pointerCursor = pointerAddress.add(1);
                }

                if (stringAddress.equals(currentProgram.getMaxAddress())) break;
                cursor = stringAddress.add(1);
            }
        }
    }

    private void reportReferences(
            String label,
            Address target,
            Listing listing,
            Memory memory,
            ReferenceManager references) {
        ReferenceIterator iterator = references.getReferencesTo(target);
        while (iterator.hasNext()) {
            Reference reference = iterator.next();
            Address from = reference.getFromAddress();
            Function function = listing.getFunctionContaining(from);
            MemoryBlock block = memory.getBlock(from);
            println(label + "|target=" + target + "|from=" + from + "|type=" + reference.getReferenceType() +
                "|block=" + (block == null ? "<none>" : block.getName()) + "|function=" +
                (function == null ? "<none>" : function.getName(true)) + "|entry=" +
                (function == null ? "<none>" : function.getEntryPoint()));
        }
    }

    private byte[] littleEndian(long value) {
        byte[] output = new byte[8];
        for (int index = 0; index < output.length; index += 1) {
            output[index] = (byte) (value >>> (index * 8));
        }
        return output;
    }
}
