import assert from "node:assert/strict";
import { it } from "node:test";
import { openForebay } from "./forebay.mts";
import { coreLibraryPath, given, weivelModule } from "./weivel.mts";

const echoLibraryPath = given("SPISOR_BINDINGS_ECHO");
const route = 1;

interface EchoMedians {
  readonly bareCall: { readonly median: number };
  readonly weivelCall: { readonly median: number };
}

const { benchmarkLibraryCall, defaultSamplings, sampleInterleaved, summarise } = await weivelModule("benchmarks");

it("measures routing.arrival through the generic binding beside weivel's echo, judged by nothing", async () => {
  const opened = await openForebay();
  try {
    const headers = [{ name: "host", value: "127.0.0.1:34567" }];
    const input = { argument: { path: "/v1/messages", headers, port: 34567 } };
    assert.equal(((await opened.routing.arrival!(input)) as { kind: number }).kind, route);

    const paths = { arrival: () => opened.routing.arrival!(input) };
    const arrival = summarise(sampleInterleaved(paths, defaultSamplings.describedCall).arrival).median as number;
    const echo = (await benchmarkLibraryCall({ echoLibraryPath, coreLibraryPath })) as EchoMedians;

    console.log(
      `spisor-bindings-measurement language=typescript platform=${process.platform}-${process.arch} ` +
        `routing.arrival=${arrival.toFixed(0)}ns echo=${echo.weivelCall.median.toFixed(0)}ns ` +
        `bare-echo=${echo.bareCall.median.toFixed(0)}ns ratio=${(arrival / echo.weivelCall.median).toFixed(1)}`,
    );
    assert.ok(arrival > 0 && echo.weivelCall.median > 0);
  } finally {
    await opened.close();
  }
});
