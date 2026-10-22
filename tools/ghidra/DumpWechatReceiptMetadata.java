// Dumps Weixin receipt parser keys and nearby string constants.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.List;

public class DumpWechatReceiptMetadata extends GhidraScript {
    private static final List<String> ADDRESSES = List.of(
        "188ed5118",
        "188ed5120",
        "188ed5130",
        "188ed5140",
        "188ed5150",
        "188ed5160",
        "188d9fd90",
        "188d9fda0",
        "18924fe18",
        "18924fe20",
        "18924fe30",
        "18924fe40",
        "18924fe50",
        "18924fe60",
        "18924fe70",
        "18924fe78",
        "18924fe88",
        "18924fea0",
        "18924feb0",
        "18924fec0"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        for (String value : ADDRESSES) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            MemoryBlock block = memory.getBlock(address);
            println(
                "STRING|address=" + address+
                "|block=" + (block == null ? "<none>" : block.getName())+
                "|ascii=" + clean(readAscii(memory, address))+
                "|hex=" + hex(memory, address, 32)
            );
        }
    }

    private String readAscii(Memory memory, Address address) {
        StringBuilder output = new StringBuilder();
        try {
            for (int index = 0; index < 128; index++) {
                int value = memory.getByte(address.add(index)) & 0xff;
                if (value == 0) break;
                if (value < 0x20 || value > 0x7e) return "";
                output.append((char) value);
            }
        } catch (Exception ignored) {
            return "";
        }
        return output.toString();
    }

    private String hex(Memory memory, Address address, int length) {
        byte[] bytes = new byte[length];
        try {
            int count = memory.getBytes(address, bytes);
            StringBuilder output = new StringBuilder();
            for (int index = 0; index < count; index++) {
                if (output.length() > 0) output.append(' ');
                output.append(String.format("%02x", bytes[index] & 0xff));
            }
            return output.toString();
        } catch (Exception ignored) {
            return "";
        }
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
