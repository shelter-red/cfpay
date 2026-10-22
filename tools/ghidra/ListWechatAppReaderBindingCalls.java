// Lists direct call instructions inside confirmed AppReader binding functions.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.FlowType;

import java.util.List;

public class ListWechatAppReaderBindingCalls extends GhidraScript {
    private static final List<String> ENTRIES = List.of(
        "184646e50",
        "184644ae0",
        "184645fb0",
        "184644910"
    );

    @Override
    protected void run() throws Exception {
        for (String entry : ENTRIES) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(entry);
            Function function = currentProgram.getListing().getFunctionAt(address);
            if (function == null) {
                println("FUNCTION_MISSING|requested=" + entry);
                continue;
            }

            println("FUNCTION_BEGIN|entry=" + function.getEntryPoint() + "|name=" + function.getName(true));
            InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
            int callCount = 0;
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                FlowType flowType = instruction.getFlowType();
                if (!flowType.isCall()) continue;
                Address[] targets = instruction.getFlows();
                if (targets.length == 0) {
                    println("CALL|from=" + instruction.getAddress() + "|target=<indirect>|assembly=" + clean(instruction.toString()));
                    callCount += 1;
                    continue;
                }
                for (Address target : targets) {
                    Function called = currentProgram.getFunctionManager().getFunctionAt(target);
                    println("CALL|from=" + instruction.getAddress() + "|target=" + target + "|name=" +
                        (called == null ? "<none>" : called.getName(true)) + "|assembly=" + clean(instruction.toString()));
                    callCount += 1;
                }
            }
            println("FUNCTION_END|entry=" + function.getEntryPoint() + "|calls=" + callCount);
        }
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
