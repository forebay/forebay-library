import { readFileSync } from "node:fs";
import { coreLibraryPath, given, standardLibraryPath, weivelModule } from "./weivel.mts";

export const forebayLibraryPath = given("SPISOR_BINDINGS_LIBRARY");
const declarationPath = given("SPISOR_BINDINGS_DECLARATION");
export const routingServiceId = "routing";

const { Host } = await weivelModule("host");
const { EdgeSource } = await weivelModule("core");
const { loadStandard } = await weivelModule("standard");

export interface Bound {
  readonly [operation: string]: (argument?: object) => Promise<unknown>;
}

export interface DeclaredService {
  readonly id: string;
  readonly description?: string;
  readonly functions: readonly { readonly symbol: string }[];
}

export interface Declaration {
  readonly services: readonly DeclaredService[];
}

/** forebay-library loaded on a host given the standard's check, its routing definition bound from its description. */
export interface OpenedForebay {
  readonly declaration: Declaration;
  readonly routing: Bound;
  close(): Promise<void>;
}

/** Loads forebay-library from its declaration as any host does. */
export async function openForebay(): Promise<OpenedForebay> {
  const standard = loadStandard(standardLibraryPath);
  const host = Host.create({
    edgeSource: EdgeSource.Capabilities,
    coreLibraryPath,
    descriptionCheck: (description: object) => standard.checkDescription(description),
    log: { debug() {}, info() {}, warn() {}, error() {} },
  });
  const read = standard.readLibrary(readFileSync(declarationPath, "utf8"));
  if (read.value === undefined) {
    throw new Error(`the standard refused forebay-library's declaration: ${read.refusal?.detail}`);
  }
  const declaration = read.value as Declaration;
  const described = host.loadLibrary(declaration, forebayLibraryPath).described as ReadonlyMap<string, Bound>;
  return {
    declaration,
    routing: described.get(routingServiceId)!,
    async close() {
      await host.destroy();
    },
  };
}
