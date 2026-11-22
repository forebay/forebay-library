import com.weivel.benchmarks.LibraryCallBenchmark;
import com.weivel.benchmarks.PairMeasurement;
import com.weivel.benchmarks.Summary;
import com.weivel.core.Answer;
import com.weivel.core.BoundDefinition;
import com.weivel.core.CoreBinary;
import com.weivel.core.CoreLoad;
import com.weivel.core.DescribedAnswer;
import com.weivel.core.DescribedHandle;
import com.weivel.host.Host;
import com.weivel.host.HostOptions;
import com.weivel.host.LibraryDeclaration;
import com.weivel.host.LoadedLibrary;
import com.weivel.standard.Standard;
import com.weivel.standard.StandardBinary;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.StructLayout;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * forebay-library bound from its descriptions by weivel/java's generic binding, as a Java host binds any described C
 * library: routing's step answering a call of a provider with the {@code forebay_walk} it hands back, the walk taken
 * back by the next step, and a description whose member offset disagrees with the platform's layout refused; with the
 * argument {@code cost}, the cost of routing.arrival printed beside the core's echo.
 */
public final class ForebayBinding {
    private static final String ROUTING = "routing";
    private static final String REQUESTED = "acme-large-1";
    private static final long CALL_PROVIDER = 3;
    private static final long THROWN = 4;
    private static final long WARN = 2;
    private static final long ROUTE = 1;
    private static final int WARMUP_CALLS = 100_000;
    private static final int CALLS_PER_BATCH = 200;
    private static final int ROUNDS = 201;
    private static final List<String> RUNNING = List.of("alpha", "beta");

    private static final StructLayout DESCRIPTION = MemoryLayout.structLayout(
            ValueLayout.JAVA_INT.withName("size"), MemoryLayout.paddingLayout(4),
            ValueLayout.ADDRESS.withName("definition"), ValueLayout.ADDRESS.withName("types"),
            ValueLayout.JAVA_LONG.withName("type_count"), ValueLayout.ADDRESS.withName("enums"),
            ValueLayout.JAVA_LONG.withName("enum_count"), ValueLayout.ADDRESS.withName("operations"),
            ValueLayout.JAVA_LONG.withName("operation_count"), ValueLayout.ADDRESS.withName("blocking"),
            ValueLayout.JAVA_LONG.withName("blocking_count"), ValueLayout.ADDRESS.withName("definitions"),
            ValueLayout.JAVA_LONG.withName("definition_count"));
    private static final StructLayout TYPE = MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("name"), ValueLayout.JAVA_LONG.withName("size"),
            ValueLayout.JAVA_LONG.withName("first_shape"), ValueLayout.ADDRESS.withName("members"),
            ValueLayout.JAVA_LONG.withName("member_count"));
    private static final StructLayout MEMBER = MemoryLayout.structLayout(
            ValueLayout.ADDRESS.withName("name"), ValueLayout.JAVA_LONG.withName("offset"),
            ValueLayout.JAVA_LONG.withName("size"), ValueLayout.JAVA_INT.withName("kind"),
            ValueLayout.JAVA_INT.withName("element"), ValueLayout.ADDRESS.withName("type"),
            ValueLayout.ADDRESS.withName("count"), ValueLayout.ADDRESS.withName("release"));

    private static final List<String> failures = new ArrayList<>();

    private ForebayBinding() {
    }

    /** Runs every check, or with the argument {@code cost} only prints the measurement of routing.arrival. */
    public static void main(String[] arguments) throws Throwable {
        boolean measuring = arguments.length == 1 && arguments[0].equals("cost");
        if (ValueLayout.ADDRESS.byteSize() != Long.BYTES) {
            throw new IllegalStateException("weivel/description.h is laid out here for a 64-bit platform only");
        }
        Path library = Path.of(given("SPISOR_BINDINGS_LIBRARY"));
        CoreBinary core = ((CoreLoad.Loaded) CoreBinary.load(Path.of(given("SPISOR_BINDINGS_CORE")))).binary();
        Standard standard = Standard.of(ok(StandardBinary.load(Path.of(given("SPISOR_BINDINGS_STANDARD")))));
        Host host = ok(Host.create(core, HostOptions.defaults().withLog(record -> { })
                .withDescriptionCheck(standard.descriptionCheck())));
        try {
            LibraryDeclaration declaration = standard.readLibrary(
                    Files.readString(Path.of(given("SPISOR_BINDINGS_DECLARATION")))).value().orElseThrow();
            LoadedLibrary loaded = ok(host.loadLibrary(library, declaration));
            BoundDefinition routing = (BoundDefinition) loaded.definitions().get(ROUTING);
            if (measuring) {
                measure(core, routing);
            } else {
                stepsWalkTheChain(routing);
                movedMemberIsRefused(library, declaration);
            }
        } finally {
            host.destroy().toCompletableFuture().get();
            host.close();
        }
        if (!failures.isEmpty()) {
            failures.forEach(failure -> System.err.println("FAILED: " + failure));
            System.exit(1);
        }
        System.out.println("ForebayBinding: every check passed");
    }

    /** A request for a model the "large" slot serves, alpha first and beta after it. */
    private static Map<String, Object> firstStep() {
        List<Object> chain = List.of(
                Map.of("provider", "alpha", "model", "alpha-large", "name", "Alpha Large", "derived", 0L),
                Map.of("provider", "beta", "model", "beta-large", "name", "Beta Large", "derived", 0L));
        return Map.of("providers", RUNNING, "profile", Map.of(), "slots", List.of(Map.of("slot", "large", "chain",
                chain)), "model", REQUESTED, "login_command", "host-cli action session", "settings_file",
                "/home/settings.json");
    }

    private static Map<?, ?> step(BoundDefinition routing, Map<String, Object> argument) {
        return (Map<?, ?>) answered(routing.call("step", Map.of("argument", argument))).value();
    }

    private static void stepsWalkTheChain(BoundDefinition routing) {
        Map<?, ?> first = step(routing, firstStep());
        DescribedHandle walk = (DescribedHandle) first.get("walk");
        try {
            check(Long.valueOf(CALL_PROVIDER).equals(first.get("kind")),
                    "the first step calls a provider, got " + first);
            check("alpha".equals(first.get("provider")) && "alpha-large".equals(first.get("model")),
                    "the first step calls the chain's first entry, got " + first);
            check(List.of().equals(first.get("log")), "the first step logs nothing, got " + first);
            check(walk != null && walk.type().equals("forebay_walk"), "the first step answers a forebay_walk, got "
                    + first);
            if (walk != null) {
                walkFallsToTheNextEntry(routing, walk);
            }
        } finally {
            if (walk != null) {
                walk.release();
            }
        }
    }

    private static void walkFallsToTheNextEntry(BoundDefinition routing, DescribedHandle walk) {
        Map<?, ?> next = step(routing, Map.of("providers", RUNNING, "walk", walk, "report",
                Map.of("kind", THROWN, "message", "boom")));
        DescribedHandle second = (DescribedHandle) next.get("walk");
        try {
            check(Long.valueOf(CALL_PROVIDER).equals(next.get("kind")) && "beta".equals(next.get("provider"))
                    && "beta-large".equals(next.get("model")), "a thrown entry falls to the next, got " + next);
            List<?> log = (List<?>) next.get("log");
            String warned = "alpha failed alpha-large, so the next chain entry serves " + REQUESTED + ": boom";
            check(log.size() == 1 && Long.valueOf(WARN).equals(((Map<?, ?>) log.get(0)).get("level"))
                    && warned.equals(((Map<?, ?>) log.get(0)).get("message")), "the fall is warned, got " + log);
            check(second != null, "the next step answers a walk of its own");
        } finally {
            if (second != null) {
                second.release();
            }
        }
    }

    private static void movedMemberIsRefused(Path library, LibraryDeclaration declaration) throws Throwable {
        List<String> symbols = declaredSymbols(declaration);
        try (Arena arena = Arena.ofConfined()) {
            SymbolLookup lookup = SymbolLookup.libraryLookup(library, arena);
            MemorySegment definition = getter(lookup, symbols.get(0));
            MemorySegment description = getter(lookup, symbols.get(1));
            MemorySegment unmoved = withMemberMoved(arena, description, "forebay_step_answer", "walk", 0);
            MemorySegment moved = withMemberMoved(arena, description, "forebay_step_answer", "walk", 8);

            check(BoundDefinition.bindDefinition(definition, unmoved).operationNames().contains("step"),
                    "an unmoved copy of routing's description binds");
            try {
                BoundDefinition.bindDefinition(definition, moved);
                check(false, "a description with forebay_step_answer.walk moved by 8 bytes was bound");
            } catch (IllegalArgumentException refused) {
                check(refused.getMessage().contains("forebay_step_answer.walk is described at offset"),
                        "the refusal names the member: " + refused.getMessage());
            }
        }
    }

    /** The getter and description symbols the declaration names for routing. */
    private static List<String> declaredSymbols(LibraryDeclaration declaration) {
        LibraryDeclaration.Service service = declaration.services().stream()
                .filter(candidate -> candidate.id().equals(ROUTING)).findFirst().orElseThrow();
        return List.of(service.functions().get(0).symbol(), service.description().orElseThrow());
    }

    private static MemorySegment getter(SymbolLookup lookup, String symbol) throws Throwable {
        MethodHandle handle = Linker.nativeLinker().downcallHandle(lookup.find(symbol).orElseThrow(),
                FunctionDescriptor.of(ValueLayout.ADDRESS));
        return (MemorySegment) handle.invokeExact();
    }

    /**
     * A copy of the description in which the member of the type is described {@code by} bytes later than the library
     * lays it: the description, its types and that type's members are copied and point at one another.
     */
    private static MemorySegment withMemberMoved(Arena arena, MemorySegment at, String type, String member, long by) {
        MemorySegment head = at.reinterpret(DESCRIPTION.byteSize());
        long typeCount = head.get(ValueLayout.JAVA_LONG, offset(DESCRIPTION, "type_count"));
        MemorySegment types = head.get(ValueLayout.ADDRESS, offset(DESCRIPTION, "types"))
                .reinterpret(typeCount * TYPE.byteSize());
        long typeIndex = indexNamed(types, TYPE, typeCount, type);
        MemorySegment row = types.asSlice(typeIndex * TYPE.byteSize(), TYPE);
        long memberCount = row.get(ValueLayout.JAVA_LONG, offset(TYPE, "member_count"));
        MemorySegment members = row.get(ValueLayout.ADDRESS, offset(TYPE, "members"))
                .reinterpret(memberCount * MEMBER.byteSize());
        long memberIndex = indexNamed(members, MEMBER, memberCount, member);

        MemorySegment movedMembers = arena.allocate(members.byteSize(), Long.BYTES).copyFrom(members);
        long memberOffset = memberIndex * MEMBER.byteSize() + offset(MEMBER, "offset");
        long described = movedMembers.get(ValueLayout.JAVA_LONG, memberOffset);
        movedMembers.set(ValueLayout.JAVA_LONG, memberOffset, described + by);
        MemorySegment movedTypes = arena.allocate(types.byteSize(), Long.BYTES).copyFrom(types);
        movedTypes.set(ValueLayout.ADDRESS, typeIndex * TYPE.byteSize() + offset(TYPE, "members"), movedMembers);
        int size = head.get(ValueLayout.JAVA_INT, offset(DESCRIPTION, "size"));
        MemorySegment movedDescription = arena.allocate(size, Long.BYTES).copyFrom(at.reinterpret(size));
        movedDescription.set(ValueLayout.ADDRESS, offset(DESCRIPTION, "types"), movedTypes);
        return movedDescription;
    }

    private static long indexNamed(MemorySegment rows, StructLayout layout, long count, String name) {
        for (long index = 0; index < count; index++) {
            MemorySegment named = rows.get(ValueLayout.ADDRESS, index * layout.byteSize() + offset(layout, "name"));
            if (name.equals(named.reinterpret(Integer.MAX_VALUE).getString(0))) {
                return index;
            }
        }
        throw new IllegalStateException("the description holds no " + name);
    }

    private static long offset(StructLayout layout, String name) {
        return layout.byteOffset(MemoryLayout.PathElement.groupElement(name));
    }

    /**
     * Prints routing.arrival's median time beside the core's echo, as the binding's own library-call benchmark measures
     * it, so a CI log carries both for its platform; nothing judges them.
     */
    private static void measure(CoreBinary core, BoundDefinition routing) {
        Map<String, Object> input = Map.of("argument", Map.of("path", "/v1/messages", "headers",
                List.of(Map.of("name", "host", "value", "127.0.0.1:34567")), "port", 34567L));
        Map<?, ?> answer = (Map<?, ?>) answered(routing.call("arrival", input)).value();
        check(Long.valueOf(ROUTE).equals(answer.get("kind")), "a loopback request is routed, got " + answer);
        for (int call = 0; call < WARMUP_CALLS; call++) {
            routing.call("arrival", input);
        }
        double[] samples = new double[ROUNDS];
        for (int round = 0; round < ROUNDS; round++) {
            long start = System.nanoTime();
            for (int call = 0; call < CALLS_PER_BATCH; call++) {
                routing.call("arrival", input);
            }
            samples[round] = (double) (System.nanoTime() - start) / CALLS_PER_BATCH;
        }
        Summary arrival = Summary.of(samples);
        PairMeasurement echo = LibraryCallBenchmark.measure(core, Path.of(given("SPISOR_BINDINGS_ECHO")),
                LibraryCallBenchmark.DEFAULT_SAMPLING);
        System.out.printf(Locale.ROOT, "spisor-bindings-measurement language=java platform=%s-%s "
                        + "routing.arrival=%.0fns echo=%.0fns bare-echo=%.0fns ratio=%.1f%n",
                System.getProperty("os.name").replace(' ', '_'), System.getProperty("os.arch"), arrival.median(),
                echo.weivel().median(), echo.bare().median(), arrival.median() / echo.weivel().median());
    }

    private static DescribedAnswer.Answered answered(DescribedAnswer answer) {
        if (!(answer instanceof DescribedAnswer.Answered answered)) {
            throw new IllegalStateException("the call did not answer: " + answer);
        }
        return answered;
    }

    private static <T> T ok(Answer<T> answer) {
        if (!(answer instanceof Answer.Ok<T> ok)) {
            throw new IllegalStateException("weivel answered " + answer);
        }
        return ok.value();
    }

    private static String given(String name) {
        String value = System.getenv(name);
        if (value == null || value.isEmpty()) {
            throw new IllegalStateException(name + " is not set; the bindings tests run under ctest, which sets it");
        }
        return value;
    }

    private static void check(boolean holds, String what) {
        if (!holds) {
            failures.add(what);
        }
    }
}
