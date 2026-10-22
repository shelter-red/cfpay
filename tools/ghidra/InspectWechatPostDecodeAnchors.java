// Extracts string, symbol and named-callee anchors for the live post-decode chain.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.util.LinkedHashSet;
import java.util.Set;

public class InspectWechatPostDecodeAnchors extends GhidraScript {
    private static final String[] ENTRIES = {
        "181948200",
        "181974590",
        "181946cd0",
        "18195b500",
        "1801d1e20",
        "181617f00",
    };

    @Override
    protected void run() throws Exception {
        for (String entry : ENTRIES) {
            Function function = currentProgram.getFunctionManager().getFunctionAt(toAddr(entry));
            if (function == null) {
                println("FUNCTION_MISSING|entry=" + entry);
                continue;
            }
            println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName());
            Set<String> emitted = new LinkedHashSet<>();
            InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                for (Reference reference : instruction.getReferencesFrom()) {
                    Address destination = reference.getToAddress();
                    Function called = currentProgram.getFunctionManager().getFunctionAt(destination);
                    if (called != null && !called.getName().startsWith("FUN_") && !called.getName().startsWith("thunk_FUN_")) {
                        emit(emitted, "CALLEE|site=" + instruction.getAddress() + "|target=" + destination +
                            "|name=" + called.getName());
                    }
                    Data data = currentProgram.getListing().getDataContaining(destination);
                    if (data != null && data.hasStringValue()) {
                        emit(emitted, "STRING|site=" + instruction.getAddress() + "|target=" + destination +
                            "|value=" + String.valueOf(data.getValue()));
                    }
                    Symbol symbol = currentProgram.getSymbolTable().getPrimarySymbol(destination);
                    if (symbol != null && !symbol.getName().startsWith("DAT_") && !symbol.getName().startsWith("UNK_")) {
                        emit(emitted, "SYMBOL|site=" + instruction.getAddress() + "|target=" + destination +
                            "|name=" + symbol.getName(true));
                    }
                }
            }
            println("FUNCTION_END|entry=" + function.getEntryPoint() + "|anchors=" + emitted.size());
        }
    }

    private void emit(Set<String> emitted, String value) {
        if (emitted.add(value)) println(value);
    }
}
