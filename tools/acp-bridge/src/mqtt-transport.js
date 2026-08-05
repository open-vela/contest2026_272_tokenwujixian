import mqtt from "mqtt";
import { MAX_MESSAGE_BYTES, assertFits, assertObject } from "./protocol.js";

export const MQTT_CAPABILITIES = Object.freeze({ ordered: true, reliable: false, encrypted: false, proximate: false, maxMessageBytes: MAX_MESSAGE_BYTES });

function topicPart(value, name) {
  if (typeof value !== "string" || !value || value.includes("+") || value.includes("#")) {
    throw new TypeError(`${name} must be a non-empty MQTT topic path without wildcards`);
  }
  return value.replace(/^\/+|\/+$/g, "");
}

export class MqttTransport {
  constructor({ brokerUrl, topicPrefix, deviceId, clientId = "deskmate-bridge", role = "bridge", username, password }) {
    if (typeof brokerUrl !== "string" || !brokerUrl) throw new TypeError("brokerUrl required");
    this.brokerUrl = brokerUrl;
    this.baseTopic = `${topicPart(topicPrefix, "topicPrefix")}/${topicPart(deviceId, "deviceId")}`;
    this.clientId = `${clientId}-${deviceId}`;
    if (role !== "bridge" && role !== "device") throw new TypeError("role must be bridge or device");
    this.role = role;
    this.username = username;
    this.password = password;
    this.client = null;
    this.handlers = null;
    this.up = false;
  }

  topics() {
    return {
      toBridge: `${this.baseTopic}/device-to-bridge`,
      toDevice: `${this.baseTopic}/bridge-to-device`,
      status: `${this.baseTopic}/status`,
    };
  }

  inboundTopic() { return this.role === "bridge" ? this.topics().toBridge : this.topics().toDevice; }
  outboundTopic() { return this.role === "bridge" ? this.topics().toDevice : this.topics().toBridge; }

  async start(handlers) {
    this.handlers = handlers;
    const topics = this.topics();
    this.client = mqtt.connect(this.brokerUrl, {
      protocolVersion: 5,
      clientId: this.clientId,
      clean: true,
      properties: { sessionExpiryInterval: 0 },
      ...(this.username ? { username: this.username } : {}),
      ...(this.password ? { password: this.password } : {}),
      will: { topic: topics.status, payload: "offline", qos: 0, retain: false },
    });
    this.client.on("message", (topic, payload) => this.#receive(topic, payload));
    this.client.on("error", (error) => handlers.onError?.("mqtt_error"));
    this.client.on("close", () => this.#setLinkState("down"));
    await new Promise((resolve, reject) => {
      const onConnect = () => {
        this.client.subscribe(this.inboundTopic(), { qos: 0 }, (error) => {
          if (error) reject(error);
          else {
            this.client.publish(topics.status, "online", { qos: 0, retain: false });
            this.#setLinkState("up");
            resolve();
          }
        });
      };
      this.client.once("connect", onConnect);
      this.client.once("error", reject);
    });
  }

  send(message) {
    if (!this.client?.connected) return;
    const payload = JSON.stringify(assertFits(assertObject(message)));
    this.client.publish(this.outboundTopic(), payload, { qos: 0, retain: false }, (error) => {
      if (error) this.handlers?.onError?.("mqtt_publish_error");
    });
  }

  async stop() {
    if (!this.client) return;
    const client = this.client;
    this.client = null;
    await new Promise((resolve) => client.end(false, { reasonCode: 0 }, resolve));
    this.#setLinkState("down");
  }

  capabilities() { return MQTT_CAPABILITIES; }

  #receive(topic, payload) {
    if (topic !== this.inboundTopic()) return;
    if (payload.length > MAX_MESSAGE_BYTES) {
      this.handlers?.onError?.("overlong_message");
      return;
    }
    try {
      const message = JSON.parse(payload.toString("utf8"));
      this.handlers?.onMessage(assertObject(message));
    } catch {
      this.handlers?.onError?.("invalid_json");
    }
  }

  #setLinkState(state) {
    if ((state === "up") === this.up) return;
    this.up = state === "up";
    this.handlers?.onLinkState?.(state);
  }
}
