import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, stat } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { mimoAcpPort, mimoAttachUrl, publishMimoAttachReady, removeMimoAttachReady } from "../src/mimo-attach.js";

test("MiMo attach ready file is atomic-looking JSON with private permissions", async () => {
  const directory = await mkdtemp(join(tmpdir(), "deskmate-mimo-attach-"));
  const path = join(directory, "state", "ready.json");
  try {
    await publishMimoAttachReady(path, {
      url: mimoAttachUrl(4096), sessionId: "ses_029ae5beaffefTC1LHP9hosoVC",
    });
    assert.deepEqual(JSON.parse(await readFile(path, "utf8")), {
      v: 1, url: "http://127.0.0.1:4096", session_id: "ses_029ae5beaffefTC1LHP9hosoVC",
    });
    assert.equal((await stat(path)).mode & 0o777, 0o600);
    await removeMimoAttachReady(path);
    await assert.rejects(stat(path));
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("MiMo attach rejects unsafe ports and ready metadata", async () => {
  assert.equal(mimoAcpPort("4096"), 4096);
  assert.throws(() => mimoAcpPort("0"));
  assert.throws(() => mimoAcpPort("65536"));
  await assert.rejects(publishMimoAttachReady("relative.json", {
    url: "http://127.0.0.1:4096", sessionId: "ses_valid",
  }));
  await assert.rejects(publishMimoAttachReady("/tmp/ready.json", {
    url: "http://localhost:4096", sessionId: "ses_valid",
  }));
});
