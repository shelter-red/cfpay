// Searches Weixin.dll for payment-related strings and reports referring functions.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.nio.charset.StandardCharsets;
import java.util.List;

public class FindWechatPaymentRefs extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "PayTransferManager",
        "paymsgid",
        "paymsg",
        "PayMsgType",
        "wcpay_info",
        "transfer_msg_type",
        "CoAcceptPayTransfer",
        "default_transfer_pay_style",
        "inactive_transfer_pay_style",
        "active_transfer_pay_style"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager references = currentProgram.getReferenceManager();
        int matches = 0;
        int xrefs = 0;

        for (String target : TARGETS) {
            byte[] bytes = target.getBytes(StandardCharsets.UTF_8);
            Address cursor = currentProgram.getMinAddress();
            while (cursor != null && !monitor.isCancelled()) {
                Address found = memory.findBytes(cursor, bytes, null, true, monitor);
                if (found == null) break;
                matches += 1;
                println("MATCH|target=" + clean(target) + "|address=" + found);

                ReferenceIterator iterator = references.getReferencesTo(found);
                while (iterator.hasNext()) {
                    Reference reference = iterator.next();
                    Function function = listing.getFunctionContaining(reference.getFromAddress());
                    String functionName = function == null ? "<none>" : function.getName(true);
                    println(
                        "XREF|target=" + clean(target) +
                        "|from=" + reference.getFromAddress() +
                        "|function=" + clean(functionName) +
                        "|entry=" + (function == null ? "<none>" : function.getEntryPoint())
                    );
                    xrefs += 1;
                }

                if (found.equals(currentProgram.getMaxAddress())) break;
                cursor = found.add(1);
            }
        }

        println("SUMMARY|matches=" + matches + "|xrefs=" + xrefs);
    }

    private String clean(String value) {
        return value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
