// Searches Weixin.dll for current-account strings and reports referring functions.
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

public class FindWechatAccountRefs extends GhidraScript {
    private static final List<String> TARGETS = List.of(
        "mars::app::AppManager::GetAccountInfo",
        "mmkv_key_nick_name",
        "mmkv_key_latest_login_username",
        "mmkv_key_user_info_before_login_",
        "account_username",
        "current_login_nick_name",
        "micromsg.UserInfoBeforeLogin",
        "micromsg.ModUserInfo",
        "upgradeToV4.kAllBetaUserInfoKey",
        "general.db",
        "contact_detail_nick_name",
        "link_profile?username=",
        "profile_info_detail"
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        Memory memory = currentProgram.getMemory();
        ReferenceManager references = currentProgram.getReferenceManager();
        int matches = 0;
        int xrefs = 0;

        for (String target : TARGETS) {
            int[] counts = search(target, "utf8", target.getBytes(StandardCharsets.UTF_8), listing, memory, references);
            matches += counts[0];
            xrefs += counts[1];
            counts = search(target, "utf16le", target.getBytes(StandardCharsets.UTF_16LE), listing, memory, references);
            matches += counts[0];
            xrefs += counts[1];
        }

        println("SUMMARY|matches=" + matches + "|xrefs=" + xrefs);
    }

    private int[] search(
            String target,
            String encoding,
            byte[] bytes,
            Listing listing,
            Memory memory,
            ReferenceManager references) throws Exception {
        int matches = 0;
        int xrefs = 0;
        Address cursor = currentProgram.getMinAddress();
        while (cursor != null && !monitor.isCancelled()) {
            Address found = memory.findBytes(cursor, bytes, null, true, monitor);
            if (found == null) break;
            matches += 1;
            println("MATCH|target=" + clean(target) + "|encoding=" + encoding + "|address=" + found);

            ReferenceIterator iterator = references.getReferencesTo(found);
            while (iterator.hasNext()) {
                Reference reference = iterator.next();
                Function function = listing.getFunctionContaining(reference.getFromAddress());
                String functionName = function == null ? "<none>" : function.getName(true);
                println(
                    "XREF|target=" + clean(target) +
                    "|encoding=" + encoding +
                    "|from=" + reference.getFromAddress() +
                    "|function=" + clean(functionName) +
                    "|entry=" + (function == null ? "<none>" : function.getEntryPoint())
                );
                xrefs += 1;
            }

            if (found.equals(currentProgram.getMaxAddress())) break;
            cursor = found.add(1);
        }
        return new int[] {matches, xrefs};
    }

    private String clean(String value) {
        return value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
