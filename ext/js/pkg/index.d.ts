// SPDX-License-Identifier: BSD-3-Clause
//
// Hand-written type declarations for tttrlib.
//
// SWIG's Node-API backend does not emit .d.ts (the SWIG JavaScript Evolution
// fork does, via %typemap(ts), but this project stays on upstream SWIG and
// writes this file by hand instead). It therefore covers the
// classes a caller reaches for first, precisely, and admits the rest through an
// index signature rather than pretending to be exhaustive and going stale
// silently. `tttrlib.native` is the untyped escape hatch.
//
// Two conventions run through everything below:
//
//   * 64-bit values are `bigint` / `BigInt64Array`, never `number`. A macro time
//     exceeds 2^53 and a `number` would round it.
//   * Every method exists under BOTH its C++ name (`get_macro_times`) and a
//     camelCase alias (`getMacroTimes`), and a `get_x`/`set_x` pair also appears
//     as a property `x`. Declared here for the camelCase and property forms,
//     which is what new code should use.

/** Flat, row-major array data. `shape` is present on 2-D and higher results. */
export type ShapedArray<T extends ArrayBufferView> = T & { shape?: number[] };

/** Anything the input typemaps accept where C++ wants a `double*`. */
export type DoubleArrayLike = Float64Array | number[] | { data: Float64Array; shape: number[] };
export type IntArrayLike = Int32Array | number[];
export type ByteArrayLike = Int8Array | number[];

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------
export declare class TTTRHeader {
  constructor();

  /** The header's full JSON payload. Assigning accepts a string or an object. */
  json: string;

  // These are %attribute-generated PROPERTIES, not getter methods -- the same
  // spelling Python uses (header.macro_time_resolution). Both the snake_case and
  // camelCase forms exist.
  /** Seconds per macro-time unit. */
  readonly macroTimeResolution: number;
  readonly macro_time_resolution: number;
  /** Seconds per micro-time channel. */
  readonly microTimeResolution: number;
  readonly micro_time_resolution: number;
  readonly numberOfMicroTimeChannels: number;
  tttrRecordType: number;
  tttrContainerType: number;

  getJson(): string;
  setJson(json: string): void;

  /** The header's tags, parsed out of the JSON payload. */
  tags(): Record<string, unknown>;

  [key: string]: any;
}

// ---------------------------------------------------------------------------
// TTTR
// ---------------------------------------------------------------------------
export declare class TTTR {
  /** Open a file, inferring the container from its content. */
  constructor(filename: string);
  /** Open a file as an explicit container type ("PTU", "HT3", "SPC-130", ...). */
  constructor(filename: string, containerType: string);
  /** Copy. */
  constructor(other: TTTR);
  /** The events of `other` at the given indices. */
  constructor(other: TTTR, selection: Int32Array);
  /** Build from raw arrays. */
  constructor(
    macroTimes: BigUint64Array,
    microTimes: Uint16Array,
    routingChannels: Int8Array,
    eventTypes: Int8Array,
  );

  size(): number;
  /** Number of events, the same value `size()` reports. */
  length(): number;

  // --- photon data (zero-copy views; see the lifetime note in jsarrays.i) ---
  /**
   * Macro times, one per event. A BigUint64Array, not a Float64Array: these
   * exceed 2^53, where a JavaScript number stops being exact. (C++ types them
   * `unsigned long long`, hence unsigned.)
   */
  readonly macroTimes: BigUint64Array;
  readonly microTimes: Uint16Array;
  readonly routingChannels: Int8Array;
  readonly eventTypes: Int8Array;
  getMacroTimes(): BigUint64Array;
  getMicroTimes(): Uint16Array;
  getRoutingChannel(): Int8Array;
  getEventType(): Int8Array;

  /** Duration of the acquisition, in seconds. */
  readonly acquisitionTime: number;

  readonly header: TTTRHeader;
  getHeader(): TTTRHeader;
  getNValidEvents(): number;
  getNumberOfMicroTimeChannels(): number;
  getUsedRoutingChannels(): Int8Array;
  getMicrotimeHistogram(coarsening?: number): [Float64Array, Float64Array];

  getMacroTimeAt(index: number): bigint;
  getMicroTimeAt(index: number): number;
  getRoutingChannelAt(index: number): number;

  // --- selections ---
  getTttrByChannel(channels: ByteArrayLike): TTTR;
  getTttrBySelection(selection: IntArrayLike): TTTR;
  getTttrByCountRate(
    timeWindow: number, nPhMax?: number, invert?: number, makeMask?: boolean): TTTR;

  /** A new TTTR over the given events: an index, a list, or {start, stop, step}. */
  select(sel: number | number[] | Int32Array | { start?: number; stop?: number; step?: number }): TTTR;
  /** A new TTTR with `other`'s events appended. */
  concat(other: TTTR): TTTR;
  append(other: TTTR): void;

  // --- burst search ---
  /**
   * Run a burst search by name, with parameters and defaults taken from
   * `registry("burst_search")`. Returns flat [start, stop, start, stop, ...].
   * Prefer this over the per-algorithm methods: it needs no hard-coded list.
   */
  burstSearchByName(algorithm: string, parameters?: Record<string, unknown>): BigInt64Array;
  burstSearch(L: number, m: number, T: number, mode?: string): BigInt64Array;
  /**
   * Bursts found independently in several detector groups at once — what
   * rejects singly-labelled and photobleached molecules in ALEX/PIE.
   * Coincidence is decided in time, not per photon.
   */
  burstSearchCoincident(
    channelGroups: Iterable<Iterable<number>>,
    algorithm?: string,
    minGroups?: number,
    L?: number,
    parameters?: Record<string, unknown> | null,
  ): BigInt64Array;

  write(filename: string, containerType?: string): boolean;

  [key: string]: any;
}

// ---------------------------------------------------------------------------
// Correlation
// ---------------------------------------------------------------------------
export declare class CorrelatorCurve {
  constructor();
  // Attributes, not setters: `cc.nBins = 3`, never `cc.setNBins(3)`.
  nBins: number;
  nCasc: number;
  n_bins: number;
  n_casc: number;
  size(): number;
  getXAxis(): Float64Array;
  getCorrelation(): Float64Array;
  [key: string]: any;
}

export declare class Correlator {
  constructor();
  setTttr(a: TTTR, b: TTTR, makeAutoCorrelation?: boolean): void;
  setMacroTimes(t1: BigUint64Array, t2: BigUint64Array): void;
  setWeights(w1: DoubleArrayLike, w2: DoubleArrayLike): void;
  nBins: number;
  nCasc: number;
  run(): void;
  getXAxis(): Float64Array;
  /** The correlation amplitude, normalised. */
  getCorrNormalized(): Float64Array;
  getCorr(): Float64Array;
  [key: string]: any;
}

// ---------------------------------------------------------------------------
// Imaging
// ---------------------------------------------------------------------------
export declare class CLSMImage {
  constructor(tttr: TTTR, ...rest: unknown[]);
  // Attributes: `img.nFrames`, not `img.getNFrames()`.
  readonly nFrames: number;
  readonly nLines: number;
  readonly nPixel: number;
  readonly nChannels: number;
  /** Photon counts per pixel, flat and row-major; `shape` is [frames, lines, pixel]. */
  getIntensity(): ShapedArray<Uint32Array>;
  getFluorescenceDecayImage(tttr: TTTR, ...rest: unknown[]): ShapedArray<Uint8Array>;
  [key: string]: any;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
/** One registry entry: a JSON-Schema-described, callable capability. */
export interface RegistryEntry {
  /** The method a caller must invoke to use this entry. */
  method: string;
  title?: string;
  description?: string;
  /** JSON Schema for the parameters, with `default`, `unit` and ranges. */
  params_schema: {
    type: 'object';
    properties: Record<string, {
      type: string;
      title?: string;
      description?: string;
      default?: unknown;
      minimum?: number;
      maximum?: number;
      enum?: unknown[];
      unit?: string;
      [key: string]: unknown;
    }>;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

/**
 * Machine-readable description of what this build of tttrlib can do.
 *
 * No argument returns every category; a category name returns its entries and
 * throws for an unknown one, naming the categories that exist. A user interface
 * built from this needs no hard-coded algorithm list and picks up new entries on
 * upgrade -- which is the reason the registry exists.
 */
export declare function registry(): Record<string, Record<string, RegistryEntry>>;
export declare function registry(category: string): Record<string, RegistryEntry>;

/** Names of the burst searches this build offers. Never hard-code this list. */
export declare function burstSearchAlgorithms(): string[];
/** Default parameters of one burst search, from its schema. */
export declare function burstSearchDefaults(algorithm: string): Record<string, unknown>;

export declare function fitModels(): Record<string, unknown>;
export declare function fitSetup(): Record<string, unknown>;
export declare function fitObjectives(): Record<string, unknown>;

// ---------------------------------------------------------------------------
// Tabular I/O
// ---------------------------------------------------------------------------
export declare class DataStore {
  constructor();
  nRows(): number;
  nColumns(): number;
  columnNames(): string[];
  [key: string]: any;
}

export interface CsvReadOptions {
  delimiter?: string;
  quote?: string;
  hasHeader?: boolean;
  /** Store inferred real columns as float32: half the memory. */
  useFloat32?: boolean;
  /** 0 to decide, 1 to force serial, or an explicit count. */
  threads?: number;
  blockSize?: number;
  /** A quoted value may contain a raw newline. Costs a sequential prescan. */
  newlinesInValues?: boolean;
  naValues?: string[];
  /** Names to keep as text regardless of what they look like. */
  textColumns?: string[];
}

export declare function readCsv(filename: string, options?: CsvReadOptions): DataStore;
/** Options for a partial table read. Each knob is native, not a slice. */
export interface TableReadOptions {
  /** Only these columns; matched per node. */
  columns?: string[];
  /** Skip this many rows of every table read. */
  firstRow?: number;
  /** How many rows, or 0 for all of them on. */
  nRows?: number;
}

export declare function readHdf5(
  filename: string,
  group?: string,
  opts?: TableReadOptions & { withGroups?: boolean },
): DataStore;

/** Bytes the HDF5 table reader has moved since the process started. */
export declare function hdf5BytesRead(): number;

export declare function loadStore(
  filename: string,
  opts?: string[] | (TableReadOptions & { group?: string }),
): DataStore;

/** Whether a `.dstore` holds this group. Reads the directory, no payload. */
export declare function storeHas(filename: string, group?: string): boolean;
export declare const Hdf5WriteMode_Update: number;
export declare const Hdf5WriteMode_Truncate: number;
export declare function writeHdf5(
  filename: string, store: DataStore, group?: string, compression?: number,
  mode?: number): boolean;

// ---------------------------------------------------------------------------
// Record streams, and the Becker & Hickl ".set" sidecar
// ---------------------------------------------------------------------------
/** What a container holds, learned without decoding any of it. */
export interface ContainerRecords {
  container_type: number;
  record_type: number;
  n_records: number;
  bytes_per_record: number;
  records_begin: number;
  /** False when this container cannot be read in pieces; `reason` says why. */
  ranged: boolean;
  reason: string;
}

/** Opaque decoder state; carries the macro time overflow count across chunks. */
export interface TTTRDecodeState {
  overflow_counter: number;
  n_records: number;
  n_events: number;
}

/**
 * Decode a buffer of undecoded records into a TTTR.
 *
 * Omitting `state` decodes `buffer` as a stream of its own, which is right for
 * a single buffer and wrong for the second chunk of one.
 */
export declare function decodeRecords(
  buffer: Uint8Array | Uint32Array, recordType: number,
  state?: TTTRDecodeState, tttr?: TTTR
): { tttr: TTTR; state: TTTRDecodeState; nEvents: number };

/**
 * Read a container's records in pieces. After the last step the yielded TTTR
 * equals `new TTTR(spec)` event for event.
 */
export declare function containerChunks(
  spec: string, chunk?: number, containerType?: number
): Generator<{ tttr: TTTR; done: number; total: number }>;

/**
 * Records [first, first + n) of a container, decoded. Macro times count from
 * `firstRecord`, not from the start of the file -- see containerChunks.
 */
export declare function containerEvents(
  spec: string, firstRecord?: number, nRecords?: number, containerType?: number
): TTTR;

/** A Becker & Hickl `.set` sidecar as `{section: {name: value}}`, all text. */
export declare function bhSet(
  filename: string, content?: string
): Record<string, Record<string, string>>;

// ---------------------------------------------------------------------------
// Module-level
// ---------------------------------------------------------------------------
/**
 * True when array results are zero-copy views over C++ memory. False means the
 * host declined external ArrayBuffers (Electron, V8-sandbox builds) or the addon
 * was built with -DTTTRLIB_JS_COPY_ARRAYS, and every array is a copy.
 */
export declare function arraysAreZeroCopy(): boolean;

/** The raw SWIG addon, for anything these declarations do not cover. */
export declare const native: Record<string, any>;

// Everything else the addon exports -- the simulator, the HMM decoders, the
// decay fits, PDA, super-resolution -- reachable but untyped. Add a declaration
// above when you start relying on one.
declare const tttrlib: {
  TTTR: typeof TTTR;
  TTTRHeader: typeof TTTRHeader;
  Correlator: typeof Correlator;
  CorrelatorCurve: typeof CorrelatorCurve;
  CLSMImage: typeof CLSMImage;
  DataStore: typeof DataStore;
  registry: typeof registry;
  [key: string]: any;
};
export default tttrlib;
