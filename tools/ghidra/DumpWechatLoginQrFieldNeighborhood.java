// Dumps nearby login QR field names and their code references.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.List;

public class DumpWechatLoginQrFieldNeighborhood extends GhidraScript {
    private static final List<String> ADDRESSES = List.of(
        "1891f9000",
        "1891f90f0",
        "1891f9108",
        "1891f9128",
        "1891f9140",
        "1891f9158",
        "1891f9170",
        "1891f9188",
        "1891f91a0",
        "1891f91b0",
        "1891f91e8",
        "1891f9208",
        "1891f9220",
        "1891f9240",
        "1891f9258",
        "1891f9270",
        "1891f9288",
        "1891f92a8",
        "1891f92c8",
        "1891f9300"
        ,"1892546a8"
        ,"1892546b8"
        ,"189254740"
        ,"189254750"
        ,"189254760"
        ,"189254768"
        ,"189254778"
        ,"189254788"
        ,"189254798"
        ,"1892547a8"
        ,"1892547c0"
        ,"1892547d8"
        ,"189254800"
        ,"1892548d8"
        ,"1892548f0"
        ,"189254900"
        ,"189254910"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        for (String value : ADDRESSES) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            String text = readAscii(memory, address);
            println("FIELD|address=" + address + "|text=" + text);
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
            while (references.hasNext()) {
                var reference = references.next();
                Function function = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
                println("XREF|address=" + address + "|from=" + reference.getFromAddress() + "|function=" +
                    (function == null ? "<none>" : function.getName(true)) + "|entry=" +
                    (function == null ? "<none>" : function.getEntryPoint()));
            }
        }
    }

    private String readAscii(Memory memory, Address address) {
        StringBuilder output = new StringBuilder();
        try {
            for (int index = 0; index < 160; index += 1) {
                int character = memory.getByte(address.add(index)) & 0xff;
                if (character == 0) break;
                if (character < 0x20 || character > 0x7e) return "";
                output.append((char) character);
            }
        } catch (Exception ignored) {
            return "";
        }
        return output.toString();
    }
}
