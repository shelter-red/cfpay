// Checks whether 4.1 account and nickname signatures survive in the 4.2 image.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class InspectWechat42KnownHooks extends GhidraScript {
    private static final byte[] ACCOUNT = hex(
        "555657534881ecd8000000488d ac248000000048c74550feffffff4889d64889".replace(" ", ""));
    private static final byte[] NICKNAME = hex(
        "554157415641545657534883ec50488d6c245048c745f8feffffff48837a3800");
    private static final byte[] ACCOUNT_CALLSITE = hex("e82d234ffe");

    @Override
    protected void run() throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) throw new IllegalStateException("PE .text section missing");
        printHits(text, "ACCOUNT", ACCOUNT);
        printHits(text, "NICKNAME", NICKNAME);
        printHits(text, "ACCOUNT_CALLSITE", ACCOUNT_CALLSITE);
        printReferences(currentProgram.getImageBase().add(0x0e07f70));
        printBytes("NEW_ACCOUNT_RVA", 0x0e07f70, ACCOUNT.length);
        printBytes("NEW_ACCOUNT_CALLSITE_RVA", 0x2915c3e, 5);
        printBytes("OLD_ACCOUNT_RVA", 0x0dc08d0, ACCOUNT.length);
        printBytes("OLD_NICKNAME_RVA", 0x004b690, NICKNAME.length);
        printBytes("OLD_CALLSITE_RVA", 0x2914d6f, ACCOUNT_CALLSITE.length);
    }

    private void printReferences(Address target) throws Exception {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
        while (references.hasNext()) {
            Reference reference = references.next();
            Function owner = currentProgram.getListing().getFunctionContaining(reference.getFromAddress());
            String ownerText = owner == null ? "unknown" : owner.getEntryPoint().toString();
            println("ACCOUNT_REFERENCE|from=" + reference.getFromAddress() + "|owner=" + ownerText +
                "|type=" + reference.getReferenceType());
            println(String.valueOf(currentProgram.getListing().getInstructionAt(reference.getFromAddress())));
        }
    }

    private void printHits(MemoryBlock block, String label, byte[] pattern) throws Exception {
        Address cursor = block.getStart();
        int count = 0;
        while (cursor != null && cursor.compareTo(block.getEnd()) <= 0 && count < 100) {
            Address match = currentProgram.getMemory().findBytes(cursor, block.getEnd(), pattern, null, true, monitor);
            if (match == null) break;
            println("SIGNATURE_HIT|name=" + label + "|rva=0x" +
                Long.toHexString(match.subtract(currentProgram.getImageBase())));
            cursor = match.add(1);
            count += 1;
        }
        println("SIGNATURE_COUNT|name=" + label + "|count=" + count);
    }

    private void printBytes(String label, long rva, int length) throws Exception {
        Address address = currentProgram.getImageBase().add(rva);
        byte[] bytes = new byte[length];
        try {
            currentProgram.getMemory().getBytes(address, bytes);
            println("BYTES|name=" + label + "|rva=0x" + Long.toHexString(rva) + "|hex=" + toHex(bytes));
        } catch (Exception error) {
            println("BYTES_UNAVAILABLE|name=" + label + "|rva=0x" + Long.toHexString(rva));
        }
    }

    private String toHex(byte[] bytes) {
        StringBuilder output = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) output.append(String.format("%02x", value & 0xff));
        return output.toString();
    }

    private static byte[] hex(String value) {
        byte[] bytes = new byte[value.length() / 2];
        for (int index = 0; index < bytes.length; index += 1) {
            bytes[index] = (byte)Integer.parseInt(value.substring(index * 2, index * 2 + 2), 16);
        }
        return bytes;
    }
}
