'use strict';

const MODULE_NAME = 'Weixin.dll';
const PARSER_OFFSET = 0x53c9350;
const MAX_STRING_BYTES = 64 * 1024;

function readStdString(address) {
  const length = address.add(0x10).readU64().toNumber();
  const capacity = address.add(0x18).readU64().toNumber();

  if (length > MAX_STRING_BYTES) {
    throw new Error(`string length ${length} exceeds limit`);
  }
  if (capacity < length) {
    throw new Error(`string capacity ${capacity} is smaller than length ${length}`);
  }
  if (length === 0) {
    return '';
  }

  const data = capacity < 0x10 ? address : address.readPointer();
  if (data.isNull()) {
    throw new Error('string data pointer is null');
  }
  return data.readUtf8String(length);
}

function readTransfer(output) {
  if (output.isNull()) {
    throw new Error('parser output pointer is null');
  }

  return {
    pay_msg_type: output.readS32(),
    from_username: readStdString(output.add(0x08)),
    to_username: readStdString(output.add(0x28)),
    content: readStdString(output.add(0x48)),
    transfer_id: readStdString(output.add(0x68)),
    transfer_msg_type: output.add(0x88).readS32(),
  };
}

const module = Process.getModuleByName(MODULE_NAME);
const hookAddress = module.base.add(PARSER_OFFSET);
const hookRange = Process.findRangeByAddress(hookAddress);

if (hookRange === null || !hookRange.protection.includes('x')) {
  throw new Error(`hook address ${hookAddress} is not executable`);
}

send({
  type: 'observer_ready',
  module_name: MODULE_NAME,
  module_path: module.path,
  module_base: module.base.toString(),
  module_size: module.size,
  hook_address: hookAddress.toString(),
  hook_offset: `0x${PARSER_OFFSET.toString(16)}`,
  hook_protection: hookRange.protection,
});

Interceptor.attach(hookAddress, {
  onEnter(args) {
    this.output = args[1];
  },
  onLeave(retval) {
    try {
      send({
        type: 'wechat_transfer',
        captured_at: new Date().toISOString(),
        thread_id: this.threadId,
        output_address: this.output.toString(),
        return_address: retval.toString(),
        fields: readTransfer(this.output),
      });
    } catch (error) {
      send({
        type: 'observer_read_error',
        captured_at: new Date().toISOString(),
        thread_id: this.threadId,
        error: String(error),
      });
    }
  },
});
