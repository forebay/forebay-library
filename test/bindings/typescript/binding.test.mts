import assert from "node:assert/strict";
import { after, before, describe, it } from "node:test";
import { openForebay, forebayLibraryPath, routingServiceId, type OpenedForebay } from "./forebay.mts";
import { CopiedDescription, withMemberMoved } from "./moved-member.mts";
import { koffi, weivelModule } from "./weivel.mts";

const callProvider = 3;
const thrown = 4;
const warn = 2;
const requested = "acme-large-1";

const { bindDefinition } = await weivelModule("host");

interface Walk {
  release(): void;
}

interface StepAnswer {
  readonly kind: number;
  readonly provider: string | null;
  readonly model: string | null;
  readonly log: readonly { readonly level: number; readonly message: string }[];
  readonly walk?: Walk;
}

let opened: OpenedForebay;

before(async () => {
  opened = await openForebay();
});

after(async () => {
  await opened?.close();
});

async function step(argument: object): Promise<StepAnswer> {
  return (await opened.routing.step!({ argument })) as StepAnswer;
}

const running = ["alpha", "beta"];

/** A request for a model the "large" slot serves, alpha first and beta after it. */
const firstStep = {
  providers: running,
  profile: {},
  slots: [
    {
      slot: "large",
      chain: [
        { provider: "alpha", model: "alpha-large", name: "Alpha Large", derived: 0 },
        { provider: "beta", model: "beta-large", name: "Beta Large", derived: 0 },
      ],
    },
  ],
  model: requested,
  login_command: "host-cli action session",
  settings_file: "/home/settings.json",
};

describe("forebay-library bound from its descriptions by weivel/typescript", () => {
  it("answers routing.step's first step as a call of the chain's first entry, with a walk to hand back", async () => {
    const answer = await step(firstStep);
    try {
      assert.equal(answer.kind, callProvider);
      assert.equal(answer.provider, "alpha");
      assert.equal(answer.model, "alpha-large");
      assert.deepEqual(answer.log, []);
      assert.ok(answer.walk !== undefined, "a call of a provider answers the forebay_walk its report hands back");
    } finally {
      answer.walk?.release();
    }
  });

  it("takes the forebay_walk a step answered back, and falls to the next entry when the first threw", async () => {
    const first = await step(firstStep);
    let next: StepAnswer | undefined;
    try {
      next = await step({ providers: running, walk: first.walk, report: { kind: thrown, message: "boom" } });

      assert.equal(next.kind, callProvider);
      assert.equal(next.provider, "beta");
      assert.equal(next.model, "beta-large");
      assert.deepEqual(next.log, [
        { level: warn, message: `alpha failed alpha-large, so the next chain entry serves ${requested}: boom` },
      ]);
      assert.ok(next.walk !== undefined);
    } finally {
      next?.walk?.release();
      first.walk?.release();
    }
  });

  it("refuses a description whose member offset disagrees with the platform's layout, and binds it unmoved", () => {
    const binary = koffi.load(forebayLibraryPath);
    const copies = new CopiedDescription();
    try {
      const service = opened.declaration.services.find((candidate) => candidate.id === routingServiceId)!;
      const definition = binary.func(service.functions[0]!.symbol, "void *", [])();
      const description = binary.func(service.description!, "void *", [])();

      const unmoved = withMemberMoved(copies, description, "forebay_step_answer", "walk", 0);
      const moved = withMemberMoved(copies, description, "forebay_step_answer", "walk", 8);

      assert.deepEqual(Object.keys(bindDefinition(definition, unmoved)), Object.keys(opened.routing));
      assert.throws(() => bindDefinition(definition, moved), /forebay_step_answer\.walk is described at offset/);
    } finally {
      copies.free();
      binary.unload();
    }
  });
});
