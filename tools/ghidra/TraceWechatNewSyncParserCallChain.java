// Decompiles the module-relative call chain captured at NewSyncResponse::Parse.
// @category CFPay

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.LinkedHashMap;
import java.util.Map;

public class TraceWechatNewSyncParserCallChain extends GhidraScript {
    private static final String[] RETURN_ADDRESSES = {
        "180680fcc",
        "1806815d5",
        "1810509b6",
        "1803494ad",
        "18282d4ce",
        "18287fdc8",
        "180dc4a3b",
        "182809b00",
        "18281de62",
        "184c70c88",
        "184c77f26",
        "184c80f2c",
        "1828e3f94",
        "1828e27d9",
        "1828e455f",
        "18011c306",
    };

    @Override
    protected void run() throws Exception {
        Map<Address, Function> selected = new LinkedHashMap<>();
        for (int index = 0; index < RETURN_ADDRESSES.length; index += 1) {
            Address returnAddress = toAddr(RETURN_ADDRESSES[index]);
            Function function = currentProgram.getFunctionManager().getFunctionContaining(returnAddress);
            println("FRAME|index=" + index + "|return=" + returnAddress + "|function=" + describe(function));
            if (function == null) continue;
            selected.putIfAbsent(function.getEntryPoint(), function);
            printCallers(function);
            printCallees(function);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Unable to open current program in decompiler");
        }
        int completed = 0;
        try {
            for (Function function : selected.values()) {
                DecompileResults results = decompiler.decompileFunction(function, 180, monitor);
                println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName() +
                    "|completed=" + results.decompileCompleted());
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    println(results.getDecompiledFunction().getC());
                    completed += 1;
                } else {
                    println("DECOMPILE_ERROR|" + results.getErrorMessage());
                }
                println("FUNCTION_END|entry=" + function.getEntryPoint());
            }
        } finally {
            decompiler.dispose();
        }
        println("SUMMARY|selected=" + selected.size() + "|completed=" + completed);
    }

    private void printCallers(Function function) {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            println("CALLER|target=" + describe(function) + "|from=" + reference.getFromAddress() +
                "|type=" + reference.getReferenceType() + "|function=" + describe(caller));
        }
    }

    private void printCallees(Function function) {
        Instruction instruction = currentProgram.getListing().getInstructionAt(function.getBody().getMinAddress());
        while (instruction != null && function.getBody().contains(instruction.getAddress())) {
            for (Reference reference : instruction.getReferencesFrom()) {
                if (!reference.getReferenceType().isCall()) continue;
                Function callee = currentProgram.getFunctionManager().getFunctionContaining(reference.getToAddress());
                println("CALLEE|source=" + describe(function) + "|at=" + instruction.getAddress() +
                    "|target=" + reference.getToAddress() + "|function=" + describe(callee));
            }
            instruction = instruction.getNext();
        }
    }

    private String describe(Function function) {
        if (function == null) return "<none>";
        return function.getName() + "@" + function.getEntryPoint();
    }
}
