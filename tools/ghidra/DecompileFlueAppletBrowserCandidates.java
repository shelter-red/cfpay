// Recovers and decompiles fixed-version WMPF browser-side applet delivery functions.
// @category CFPay

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;

import java.util.List;

public class DecompileFlueAppletBrowserCandidates extends GhidraScript {
    private static final List<Candidate> CANDIDATES = List.of(
        new Candidate("runtime_send_applet_event", "182d179f0", "182d17c4d", "182d17b0d"),
        new Candidate("runtime_session_send_data", "182d22100", "182d2244a", "182d2231d"),
        new Candidate("index_notify_event", "182d4bac0", "182d4bd4e", "182d4bb9c"),
        new Candidate("index_notify_event_sr", "182d4f100", "182d4f6af", "182d4f1ef,182d4f437"),
        new Candidate("index_notify_event_adapter", "182d4f6b0", "182d4f95f", "182d4f787"),
        new Candidate("manager_dispatch_event", "182d5a400", "182d5a627", "182d5a513"),
        new Candidate("manager_send_data_text", "182d62e70", "182d6348b", "182d631bc"),
        new Candidate("manager_send_data_value", "182d63490", "182d63811", "182d636d7")
    );

    @Override
    protected void run() throws Exception {
        Listing listing = currentProgram.getListing();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }

        try {
            for (Candidate candidate : CANDIDATES) {
                if (monitor.isCancelled()) break;
                Address start = address(candidate.start);
                Address end = address(candidate.end);
                AddressSet body = new AddressSet(start, end.subtract(1));

                DisassembleCommand command = new DisassembleCommand(start, body, true);
                command.enableCodeAnalysis(false);
                boolean disassembled = command.applyTo(currentProgram, monitor);

                Function function = listing.getFunctionAt(start);
                if (function == null) {
                    function = listing.createFunction(candidate.name, start, body, SourceType.USER_DEFINED);
                }

                println(
                    "FUNCTION_BEGIN|label=" + candidate.name+
                    "|entry=" + start+
                    "|end=" + end+
                    "|refs=" + candidate.references+
                    "|disassembled=" + disassembled+
                    "|body_bytes=" + body.getNumAddresses()
                );
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                println("DECOMPILE|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                } else {
                    println("DECOMPILE_ERROR|" + clean(results.getErrorMessage()));
                }
                println("FUNCTION_END|label=" + candidate.name + "|entry=" + start);
            }
        } finally {
            decompiler.dispose();
        }
    }

    private Address address(String value) throws Exception {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }

    private static class Candidate {
        private final String name;
        private final String start;
        private final String end;
        private final String references;

        private Candidate(String name, String start, String end, String references) {
            this.name = name;
            this.start = start;
            this.end = end;
            this.references = references;
        }
    }
}
