import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Ajv } from "ajv";
import type { ValidateFunction } from "ajv";
import { beforeAll, describe, expect, it } from "vitest";

const LIBRARY_ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const CONTRACTS = join(LIBRARY_ROOT, "contracts");
const VECTORS = join(LIBRARY_ROOT, "test", "vectors", "ir");
const SCHEMA_FILES = ["ir.schema.json", "front-door.schema.json", "provider.schema.json"];

interface Vector {
  definition: string;
  valid: boolean;
  value: unknown;
}

function readJson(path: string): any {
  return JSON.parse(readFileSync(path, "utf8"));
}

function createAjv(): Ajv {
  const ajv = new Ajv({ strict: true, allErrors: false });
  for (const file of SCHEMA_FILES) {
    ajv.addSchema(readJson(join(CONTRACTS, file)));
  }
  return ajv;
}

function definitionValidator(ajv: Ajv, file: string, definition: string): ValidateFunction {
  const id = readJson(join(CONTRACTS, file)).$id;
  const validate = ajv.getSchema(`${id}#/definitions/${definition}`);
  if (validate === undefined) {
    throw new Error(`${file} declares no definition ${definition}`);
  }
  return validate;
}

describe("the contract schemas", () => {
  let ajv: Ajv;

  beforeAll(() => {
    ajv = createAjv();
  });

  it.each(SCHEMA_FILES)("%s compiles as draft-07", (file) => {
    expect(readJson(join(CONTRACTS, file)).$schema).toBe("http://json-schema.org/draft-07/schema#");
    expect(() => createAjv()).not.toThrow();
  });

  it.each([
    ["ir.schema.json", ["IrRequest", "IrResponse", "IrBlock", "IrStreamEvent", "IrFailure"]],
    ["front-door.schema.json", ["Profile", "ProfileAnswer", "PassThrough", "DecodeRequestAnswer", "EncodeResponseAnswer", "EncodeEventAnswer", "RateLimitReplyAnswer"]],
    ["provider.schema.json", ["ModelsAnswer", "HandleIrAnswer"]],
  ] as const)("%s declares its named definitions", (file, names) => {
    for (const name of names) {
      expect(definitionValidator(ajv, file, name)).toBeTypeOf("function");
    }
  });

  const vectorFiles = readdirSync(VECTORS).filter((name) => name.endsWith(".json")).sort();

  it("has vectors for every block kind and stream event", () => {
    const kinds = new Set<string>();
    for (const file of vectorFiles) {
      const vector: Vector = readJson(join(VECTORS, file));
      const discriminator = (vector.value as any).kind ?? (vector.value as any).event;
      if (vector.valid && vector.definition !== "IrRequest" && vector.definition !== "IrResponse") {
        kinds.add(discriminator);
      }
    }
    for (const kind of ["text", "image", "tool_use", "tool_result", "thinking", "unknown", "message_start",
      "content_block_start", "text_delta", "thinking_delta", "thinking_signature", "tool_input_delta",
      "content_block_stop", "message_delta", "message_stop", "error"]) {
      expect(kinds.has(kind), kind).toBe(true);
    }
  });

  it.each(vectorFiles)("vector %s is judged as its file says", (file) => {
    const vector: Vector = readJson(join(VECTORS, file));
    const validate = definitionValidator(ajv, "ir.schema.json", vector.definition);
    expect(validate(vector.value), JSON.stringify(validate.errors)).toBe(vector.valid);
  });

  it("accepts a profile and refuses one missing its tier order", () => {
    const validate = definitionValidator(ajv, "front-door.schema.json", "Profile");
    const profile = {
      tierSourceProvider: "anthropic", tierOrder: ["haiku", "sonnet", "opus"], tierFallback: ["sonnet"],
      tierRegex: "claude-(haiku|sonnet|opus)", envPrefix: "ANTHROPIC", defaultContext: 200000,
      defaultOutput: 64000, nativeModelPattern: "^claude-", upstream: "https://api.example.test",
    };
    expect(validate(profile)).toBe(true);
    const { tierOrder, ...incomplete } = profile;
    expect(validate(incomplete)).toBe(false);
  });

  it("answers profile with a profile and refuses one missing its upstream", () => {
    const validate = definitionValidator(ajv, "front-door.schema.json", "ProfileAnswer");
    const profile = {
      tierSourceProvider: "anthropic", tierOrder: ["opus"], tierFallback: ["opus"], tierRegex: "^claude-([a-z]+)-\\d",
      envPrefix: "ANTHROPIC", defaultContext: 200000, defaultOutput: 64000, nativeModelPattern: null,
      upstream: "https://api.example.test",
    };
    expect(validate(profile), JSON.stringify(validate.errors)).toBe(true);
    const { upstream, ...incomplete } = profile;
    expect(validate(incomplete)).toBe(false);
  });

  it("lets decodeRequest pass a request through, and refuses a pass-through that is not true or carries more", () => {
    const validate = definitionValidator(ajv, "front-door.schema.json", "DecodeRequestAnswer");
    expect(validate({ passThrough: true }), JSON.stringify(validate.errors)).toBe(true);
    expect(validate({ passThrough: false })).toBe(false);
    expect(validate({ passThrough: true, status: 200 })).toBe(false);
    expect(validate({ status: 400, headers: {}, body: "bad" })).toBe(true);
  });

  it("accepts a models answer and a handleIr failure, and refuses a model without a context", () => {
    const models = definitionValidator(ajv, "provider.schema.json", "ModelsAnswer");
    expect(models({ models: [{ id: "m1", name: "M1", context: 1000, output: 100 }], defaultModelId: "m1" })).toBe(true);
    expect(models({ models: [{ id: "m1", name: "M1", output: 100 }] })).toBe(false);
    const handle = definitionValidator(ajv, "provider.schema.json", "HandleIrAnswer");
    expect(handle({ status: 503, headers: {}, body: "" })).toBe(true);
    expect(handle([{ event: "message_stop" }])).toBe(true);
    expect(handle({ status: 503 })).toBe(false);
  });
});
