// Finds AppReader UI setter calls across compiler-split binding code blocks.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.util.LinkedHashMap;
import java.util.Map;

public class FindWechatAppReaderBindingSetters extends GhidraScript {
    private static final String START = "184644000";
    private static final String END = "184647300";
    private static final Map<Long, String> TARGETS = new LinkedHashMap<>();

    static {
        TARGETS.put(0x183d3d540L, "styledText");
        TARGETS.put(0x181e84570L, "plainText");
        TARGETS.put(0x1808e4180L, "mediaData");
    }

    @Override
    protected void run() throws Exception {
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(START);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(END);
        int length = (int) (end.subtract(start) + 1);
        byte[] bytes = new byte[length];
        currentProgram.getMemory().getBytes(start, bytes);
        PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);

        int matches = 0;
        for (int index = 0; index + 5 <= bytes.length && !monitor.isCancelled(); index += 1) {
            if ((bytes[index] & 0xff) != 0xe8) continue;
            int displacement = (bytes[index + 1] & 0xff) | ((bytes[index + 2] & 0xff) << 8) |
                ((bytes[index + 3] & 0xff) << 16) | (bytes[index + 4] << 24);
            Address instructionAddress = start.add(index);
            long target = instructionAddress.getOffset() + 5L + displacement;
            String role = TARGETS.get(target);
            if (role == null) continue;
            PseudoInstruction instruction = disassembler.disassemble(instructionAddress);
            if (instruction == null || instruction.getLength() != 5 || !"CALL".equals(instruction.getMnemonicString())) continue;
            Function caller = currentProgram.getListing().getFunctionContaining(instructionAddress);
            println("SETTER_CALL|from=" + instructionAddress + "|return=" + instructionAddress.add(5) +
                "|target=" + Long.toHexString(target) + "|role=" + role + "|caller=" +
                (caller == null ? "<none>" : caller.getName(true)) + "|caller_entry=" +
                (caller == null ? "<none>" : caller.getEntryPoint()) + "|signature=" + signature(instructionAddress));
            matches += 1;
        }
        println("SUMMARY|start=" + start + "|end=" + end + "|matches=" + matches);
    }

    private String signature(Address address) throws Exception {
        byte[] bytes = new byte[32];
        currentProgram.getMemory().getBytes(address, bytes);
        StringBuilder output = new StringBuilder();
        for (byte value : bytes) {
            if (output.length() > 0) output.append(' ');
            output.append(String.format("%02x", value & 0xff));
        }
        return output.toString();
    }
}
