// Searches login and QR field strings and reports referring functions.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.nio.charset.StandardCharsets;
import java.util.List;

public class FindWechatLoginQrFieldRefs extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "login_qr",
        "login_qrcode",
        "qrcode",
        "qr_code",
        "qr_image",
        "qr_url",
        "login_url",
        "uuid",
        "LoginQRCode",
        "GetLoginQRCode",
        "CheckLoginQRCode",
        "PushLoginURL",
        "WeChatLoginWndForPC"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager references = currentProgram.getReferenceManager();
        int matches = 0;
        int xrefs = 0;
        for (String target : TARGETS) {
            for (String encoding : List.of("utf8", "utf16le")) {
                byte[] bytes = encoding.equals("utf8")
                    ? target.getBytes(StandardCharsets.UTF_8)
                    : target.getBytes(StandardCharsets.UTF_16LE);
                Address cursor = currentProgram.getMinAddress();
                while (cursor != null && !monitor.isCancelled()) {
                    Address found = memory.findBytes(cursor, bytes, null, true, monitor);
                    if (found == null) break;
                    matches += 1;
                    println("MATCH|target=" + clean(target) + "|encoding=" + encoding + "|address=" + found);
                    ReferenceIterator iterator = references.getReferencesTo(found);
                    while (iterator.hasNext()) {
                        var reference = iterator.next();
                        Function function = listing.getFunctionContaining(reference.getFromAddress());
                        println("XREF|target=" + clean(target) + "|encoding=" + encoding +
                            "|from=" + reference.getFromAddress() + "|function=" +
                            (function == null ? "<none>" : clean(function.getName(true))) +
                            "|entry=" + (function == null ? "<none>" : function.getEntryPoint()));
                        xrefs += 1;
                    }
                    if (found.equals(currentProgram.getMaxAddress())) break;
                    cursor = found.add(1);
                }
            }
        }
        println("SUMMARY|matches=" + matches + "|xrefs=" + xrefs);
    }

    private String clean(String value) {
        return value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
