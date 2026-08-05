#!/usr/bin/env node
import readline from "node:readline";
import { AcpBridge } from "./bridge.js";

const bridge = new AcpBridge();
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });

function emit(record) {
  process.stdout.write(`${JSON.stringify(record)}\n`);
}

for await (const line of input) {
  if (!line.trim()) continue;
  let record;
  try {
    record = JSON.parse(line);
  } catch {
    emit({ type: "error", error: "invalid_json" });
    continue;
  }

  try {
    if (record.type === "acp_request_permission") {
      emit(bridge.ptSubmit({ ...record.params, requestId: record.id }));
    } else if (record.type === "device_decision") {
      const result = bridge.receiveDeviceDecision(record);
      if (result.type === "acp_response") {
        emit(result.deviceResult);
        emit({ jsonrpc: "2.0", id: result.id, result: result.result });
      } else {
        emit(result);
      }
    } else if (record.type === "disconnect") {
      bridge.disconnect();
      emit({ type: "offline" });
    } else {
      emit({ type: "error", error: "unsupported_record" });
    }
  } catch (error) {
    emit({ type: "error", error: "invalid_request", detail: error.message });
  }
}
