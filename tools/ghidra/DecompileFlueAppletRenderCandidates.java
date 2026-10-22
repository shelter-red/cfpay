// Recovers and decompiles fixed-version WMPF applet render boundary functions.
// @category CFPay

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.SourceType;

import java.util.List;

public class DecompileFlueAppletRenderCandidates extends GhidraScript {
    private static final List<Candidate> CANDIDATES = List.of(
        new Candidate("notify_app_service_event", "182dcf450", "182dcf6a7", "182dcf4a7"),
        new Candidate("mini_program_invoke_handler", "182f07090", "182f0753d", "182f07208,182f074a2"),
        new Candidate("renderer_invoke_service_callback", "182f07920", "182f07c8e", "182f0799e,182f07c05"),
        new Candidate("render_jsapi_do", "182f1fc60", "182f20047", "182f1fd99"),
        new Candidate("render_jsapi_sync", "182f20050", "182f201f3", "182f200ef"),
        new Candidate("render_jsapi_async", "182f20200", "182f20405", "182f202dc"),
        new Candidate("xweb_invoke_callback", "183749e80", "18374a61b", "18374a001"),
        new Candidate("xweb_run_invoke_callback", "183751810", "183751af9", "183751a7a"),
        new Candidate("renderer_set_data", "1837eab90", "1837ec149", "1837eb817")
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
                printCalls(listing, function);

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

    private void printCalls(Listing listing, Function function) {
        InstructionIterator instructions = listing.getInstructions(function.getBody(), true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            FlowType flowType = instruction.getFlowType();
            if (!flowType.isCall()) continue;
            Address[] targets = instruction.getFlows();
            if (targets.length == 0) {
                println(
                    "CALL|from=" + instruction.getAddress()+
                    "|target=<indirect>"+
                    "|assembly=" + clean(instruction.toString())
                );
                continue;
            }
            for (Address target : targets) {
                Function called = currentProgram.getFunctionManager().getFunctionAt(target);
                println(
                    "CALL|from=" + instruction.getAddress()+
                    "|target=" + target+
                    "|name=" + (called == null ? "<none>" : called.getName(true))+
                    "|assembly=" + clean(instruction.toString())
                );
            }
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
